"""Grid access for the traduku E2E suite (see ROADMAP.md §6).

Host-side helper: SSH into a grid VM (QEMU, port 2200+idx) + QMP virtual
keyboard/screenshots through tools/vms/vmctl.sh (docker exec, the QMP socket
is root-owned). One Grid per VM; tests declare `TRADUKU_VM`.
"""
import os
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VMCTL = ROOT / "tools" / "vms" / "vmctl.sh"
VMS_HOME = Path.home() / "vms"

# The suite used to be "slow" and every fix started with a guess, so the
# harness measures itself. Every guest command is timed; anything over SLOW_S
# shouts while the run is in progress, and conftest.pytest_terminal_summary
# prints the slowest ones at the end. Percentiles and "the suite takes 5 min"
# are not actionable - "this one ssh takes 0.9s, 180 times" is.
SLOW_S = 0.5
SLOWLOG = []          # [(seconds, vm, command)] - appended by Grid.run
_GRIDS = []           # live Grids, so the session can close their sockets


class Grid:
    def __init__(self, vm):
        self.vm = vm
        idx = self._idx()
        self.port = 2200 + idx
        self.shots = VMS_HOME / "run" / vm / "shots"
        # One multiplexed TCP+auth connection per VM, reused by every command
        # (ControlPersist keeps it warm between them; a reboot kills it and the
        # next command reconnects by itself). Measured on ubuntu-gnome: a
        # fresh handshake per command cost 0.35-0.5s, and the autouse fixture
        # alone made ~8 of them per test - 88% of that fixture's time was
        # ssh authenticating, not the guest working.
        self.control = f"/tmp/traduku-grid-{vm}-{os.getpid()}.sock"
        self.last_stderr = ""
        _GRIDS.append(self)

    def _idx(self):
        out = self.vmctl("status")
        for line in out.splitlines():
            f = line.split()
            if f and f[0] == self.vm:
                return int(f[2].split("=")[1])
        raise RuntimeError(f"vm {self.vm} not in grid:\n{out}")

    def vmctl(self, *args):
        return subprocess.run([str(VMCTL), *args], capture_output=True, text=True).stdout

    # ---- ssh ----
    def _ssh_opts(self):
        return ["ssh", "-i", str(VMS_HOME / "id_ed25519"),
                "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                "-o", "ConnectTimeout=10", "-o", "ServerAliveInterval=15",
                "-o", "ControlMaster=auto", f"-oControlPath={self.control}",
                "-o", "ControlPersist=180",
                "-p", str(self.port), "vm@127.0.0.1"]

    def _ssh_cmd(self, cmd):
        return self._ssh_opts() + [cmd]

    def close_master(self, quiet=False):
        """Drop the multiplexed connection."""
        p = subprocess.run(self._ssh_opts() + ["-O", "exit"],
                           capture_output=True, text=True, timeout=20)
        if not quiet and p.returncode != 0 and p.stderr.strip():
            print(f"[grid] closing the master for {self.vm}: {p.stderr.strip()}", flush=True)
        # A master that died with the guest leaves its socket behind; ssh would
        # keep trying to talk to it.
        try:
            os.unlink(self.control)
        except OSError:
            pass

    def run(self, cmd, timeout=120, _retries=1):
        """Run in the guest; returns (rc, stdout).

        Timed, and loud when slow: a suite nobody can profile gets "fixed" by
        raising timeouts, which makes it slower and redder at once.

        rc 255 is ssh talking about itself - the transport died - not the
        guest's exit code. test_03 reboots the VM, which takes the multiplexed
        connection with it while ControlPersist leaves the socket behind, and
        the next command then failed with 255 instead of reconnecting (measured:
        the whole rest of the suite died in "session.env: No such file" and
        scp 255, because the one file that lives in /tmp was gone with the
        reboot and nothing could rewrite it). Before multiplexing, every command
        made its own connection and this could not happen - so dropping the
        master and retrying once is the price of the speedup, not a workaround.
        """
        started = time.monotonic()
        p = subprocess.run(self._ssh_cmd(cmd), capture_output=True, text=True, timeout=timeout)
        took = time.monotonic() - started
        SLOWLOG.append((took, self.vm, cmd))
        if took >= SLOW_S:
            print(f"[slow {took:5.2f}s] {self.vm}: {cmd[:150]}", flush=True)
        self.last_stderr = p.stderr
        if p.returncode == 255 and _retries > 0:
            print(f"[grid] {self.vm}: ssh transport died ({p.stderr.strip()[:90]}); "
                  "dropping the master and retrying once", flush=True)
            self.close_master(quiet=True)
            return self.run(cmd, timeout, _retries - 1)
        return p.returncode, p.stdout

    def ok(self, cmd, timeout=120):
        rc, out = self.run(cmd, timeout)
        return rc == 0, out

    def _scp_base(self):
        # Same multiplexing as run(): scp over a warm master is milliseconds,
        # and it is the same path that has to be dropped when the guest reboots.
        return ["scp", "-i", str(VMS_HOME / "id_ed25519"),
                "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                "-o", "ControlMaster=auto", f"-oControlPath={self.control}",
                "-o", "ControlPersist=180",
                "-P", str(self.port)]

    def push(self, local, remote):
        for attempt in (0, 1):
            p = subprocess.run(self._scp_base() + [str(local), f"vm@127.0.0.1:{remote}"],
                               capture_output=True, text=True, timeout=120)
            if p.returncode == 0:
                return
            if p.returncode == 255 and attempt == 0:
                self.close_master(quiet=True)
                continue
            raise RuntimeError(f"scp {local} -> {remote} failed ({p.returncode}): {p.stderr}")

    def pull(self, remote, local):
        for attempt in (0, 1):
            p = subprocess.run(self._scp_base() + [f"vm@127.0.0.1:{remote}", str(local)],
                               capture_output=True, text=True, timeout=120)
            if p.returncode == 0:
                return
            if p.returncode == 255 and attempt == 0:
                self.close_master(quiet=True)
                continue
            raise RuntimeError(f"scp {remote} -> {local} failed ({p.returncode}): {p.stderr}")

    # ---- session ----
    def wait_ssh(self, seconds=420, steady=3):
        """Wait until ssh answers the same way `steady` times in a row.

        One `echo up` only proves sshd opened its port. On this image, right
        after `systemctl reboot`, sshd keeps dropping the next connections
        ("Connection closed by 127.0.0.1", "timed out during banner exchange"),
        and every test after the reboot died on that - which is what the 15 s of
        sleep this suite used to carry after wait_ssh was really paying for.
        Three answers ~1 s apart is what "the machine is up" has always meant
        here, and it costs two seconds.
        """
        end = time.time() + seconds
        ok = 0
        while time.time() < end:
            try:
                rc, _ = self.run("echo up", timeout=15)
                ok = ok + 1 if rc == 0 else 0
            except subprocess.TimeoutExpired:
                ok = 0
            if ok >= steady:
                return True
            time.sleep(1)
        return False

    def sudo(self, cmd):
        return self.run(f"echo vm | sudo -S {cmd}")

    def clip_tool(self, op):
        """The clipboard tool this *session* can talk to: 'wl-*' or 'xclip'.

        Chosen from the running session, not from the package list. Measured on
        debian-kde: that image boots a native X11 Plasma (kwin_x11 + Xorg on
        vt2, no kwin_wayland and no Xwayland), and the image has wl-clipboard
        installed anyway - so `command -v wl-paste` is true and useless, the
        compositor it would ask for is not there, and every clipboard test died
        on an empty string that looked like a product bug. The session tells us
        which one it is: WAYLAND_DISPLAY for a Wayland compositor, DISPLAY for
        an X one.

        Returns 'wayland', 'x11' or 'none'; 'none' means the harness has no
        reader, which callers must report as such instead of asserting.
        """
        cached = getattr(self, "_clip_backend", None)
        if cached:
            return cached
        rc, out = self.ses(
            'if [ -n "$WAYLAND_DISPLAY" ] && command -v wl-paste >/dev/null 2>&1; then echo wayland; '
            'elif [ -n "$DISPLAY" ] && command -v xclip >/dev/null 2>&1; then echo x11; '
            'else echo none; fi')
        lines = [l.strip() for l in out.strip().splitlines() if l.strip()]
        self._clip_backend = lines[-1] if lines else "none"
        return self._clip_backend

    def clipset(self, text):
        """Seed the DE clipboard with what the session actually speaks.

        The two sessions differ in who *keeps* the clipboard, and the first
        version of this ignored it. On Wayland the compositor stores the
        selection, so `timeout 10 wl-copy` is fine - the data outlives the
        tool. On X11 the client that owns CLIPBOARD *is* the clipboard: a
        `timeout 10 xclip -i` clipboard evaporates ten seconds later, which is
        mid-test for a gesture that must be detected, translated, streamed and
        written back - and the test then blames the app for an empty clipboard
        that the harness itself deleted.

        So on X11: hand the bytes through a file, let xclip detach, and put it
        in its own session so it outlives the ssh connection that started it
        (an ssh-owned holder dies with the connection, taking the selection
        with it). Old holders go first: two owners means the reader gets
        whichever one answers last.
        """
        if self.clip_tool("copy") == "x11":
            self.ses(f"printf %s '{text}' > /tmp/traduku-clipseed.txt")
            self.ses("pkill -x xclip >/dev/null 2>&1; true")
            return self.ses(
                "setsid xclip -selection clipboard -i /tmp/traduku-clipseed.txt "
                ">/dev/null 2>&1 </dev/null &")
        return self.ses(f"printf %s '{text}' | timeout 10 wl-copy >/dev/null 2>&1")

    def start_app(self, extra_env="", ready_timeout=15):
        """(Re)launch the packaged app and wait until it says it is up.

        systemd-run, NOT `setsid nohup >file`: journal_after() (gesture
        freshness in tests 05/06 and the KGA warm-up gate) reads
        journalctl, and a file-redirected app silently loses every
        'translating' line there - arch only ever looked green because
        the autostart-unit instance (journal-captured) also runs in the
        session and produced the lines. One unit, both distros, one
        logging truth. --collect removes the unit on exit so the next
        start is clean; --setenv forwards the session trio captured by
        capture_sessionenv.

        Three round trips, all event-driven: read the journal cursor, one
        script that stops the old instance and starts the unit, one script that
        waits for the product's own "traduku: ready" line (src/app/AppFlavour.h
        logReady). That line replaced `pkill; sleep 1` in here plus the fixture
        's `sleep 4` + `sleep 1` - six seconds of guessing in every test, and
        still red on a loaded VM. The polling loops run in the GUEST, so their
        0.1 s tick costs no ssh round trip; a tick inside a bounded loop is a
        wait, and the blind fixed pauses it replaces were the bug.
        """
        cursor = self.cursor()
        # full session environment through the wrapper: KWindowSystem /
        # the KGA backends branch on XDG_SESSION_TYPE/XDG_CURRENT_DESKTOP,
        # --setenv of the display trio alone silently picked the wrong
        # (x11) path on arch and global shortcuts stopped arriving.
        # extra_env is for the cases where the environment IS the test
        # (QT_SCALE_FACTOR=2 for HiDPI); it goes through --setenv, not through
        # the shell, so it survives the exec and lands on the app itself.
        env_args = " ".join(f"--setenv={e}" for e in extra_env) if extra_env else ""
        self.ses(
            self._alive_fn()
            + "systemctl --user stop traduku-e2e.service >/dev/null 2>&1; "
              "pkill -x traduku 2>/dev/null; "
              "end=$((SECONDS + 8)); "
              "while [ $SECONDS -lt $end ] && alive; do sleep 0.1; done; "
            f"systemd-run --user --collect {env_args} --unit=traduku-e2e.service "
            "sh -c '. /tmp/session.env; exec /usr/bin/traduku'")
        rc, out = self.wait_journal(cursor, "traduku: ready", timeout=ready_timeout,
                                   unit="traduku-e2e.service")
        if rc != 0:
            _, tail = self.ses(
                f"journalctl --user --no-pager --after-cursor='{cursor}' "
                "-u traduku-e2e.service 2>/dev/null | tail -15; "
                "pgrep -a traduku | head -3")
            raise RuntimeError(
                f"traduku never reported ready on {self.vm} within {ready_timeout}s. "
                "Either a backend it needs is missing (the tail below names it) or "
                "the app is wedged before finishing startup.\n" + tail)
        return out

    def wait_no_instance(self, timeout=10.0, tick=0.2):
        """Wait until no LIVE traduku is left: (rc, who). `who` names the live
        processes that remain, and a <defunct> is not one of them.

        Zombie-aware for the same reason app_alive() is: an exited child of a
        systemd unit lingers until it is reaped, `pgrep` lists it, and a check
        that counts it reports "an instance you did not start is running" about
        a process that is only a row in the process table (measured on
        ubuntu-gnome: `1415 [traduku] <defunct>`).
        """
        alive = ('alive() { for p in $(pgrep -x traduku 2>/dev/null); do '
                 's=$(cut -d" " -f3 /proc/$p/stat 2>/dev/null); '
                 '[ -n "$s" ] && [ "$s" != Z ] && return 0; done; return 1; }; ')
        rc, _ = self._wait_cmd(alive + self._wait_loop("! alive", timeout, tick),
                               timeout, tick)
        _, who = self.run(
            'for p in $(pgrep -x traduku 2>/dev/null); do '
            's=$(cut -d" " -f3 /proc/$p/stat 2>/dev/null); '
            '[ "$s" = Z ] || ps -o pid=,args= -p $p; done | head -3')
        return rc, who.strip()

    def screen_size(self):
        """The compositor's scanout size, from the same QMP screendump the
        framebuffer diffs use. Deliberately not the X screen: a Wayland client
        is told what Xwayland looks like, which is not what a window has to
        fit into."""
        head = self.shot("screen-size").read_bytes()[:64].split()
        return int(head[1]), int(head[2])

    @staticmethod
    def _ses_prefix():
        return "source /tmp/session.env; "

    def ses(self, script, timeout=120):
        """Run a snippet inside the captured graphical session."""
        return self.run(self._ses_prefix() + script, timeout=timeout)

    # ---- waiting for an event, not for a clock ---------------------------
    def _alive_fn(self, name="traduku"):
        """A shell function `alive`: true while a NON-zombie `name` process runs.

        Two traps, both measured on ubuntu-gnome.

        pgrep alone lies. A traduku killed but not reaped stays in the process
        table as state Z, keeps the name and is counted - a zombie sat next to
        the live app through the whole suite - so every "is it running?"
        question here (this restart, and "did autostart bring it back after
        login?") could be answered by a corpse.

        It has to be a *function*: bash rejects a compound command in the
        `while cond && { ... }` position ("syntax error near unexpected token
        `do`" - the whole ssh command then died with rc 2 and the app was never
        started), and an inline group whose body ran `exit` would have ended the
        session instead of just the test.
        """
        return (f'alive() {{ for p in $(pgrep -x {name} 2>/dev/null); do '
                f's=$(cut -d" " -f3 /proc/$p/stat 2>/dev/null); '
                f'[ -n "$s" ] && [ "$s" != Z ] && return 0; done; return 1; }}; ')

    def app_pid(self, name="traduku"):
        """pid of a live (non-zombie) `name`, or ""."""
        _, out = self.run(
            f"for p in $(pgrep -x {name} 2>/dev/null); do "
            f's=$(cut -d" " -f3 /proc/$p/stat 2>/dev/null); '
            f'[ -n "$s" ] && [ "$s" != Z ] && {{ echo $p; exit 0; }}; done; exit 0')
        return out.strip().splitlines()[-1] if out.strip() else ""

    def app_alive(self, name="traduku"):
        """(rc, out): rc 0 while a live `name` process exists."""
        return self.run(self._alive_fn(name) + "alive")

    def _wait_loop(self, test_cmd, timeout, tick):
        """A bounded guest-side poll. ONE ssh for the whole wait: the loop, and
        its tick, live in the guest, so checking 20x a second is free. The tick
        is not a fixed pause - the loop leaves the moment the event is true.
        """
        return (f"end=$((SECONDS + {int(timeout)})); "
                f"while [ $SECONDS -lt $end ]; do "
                f"if {test_cmd}; then echo waited $SECONDS; exit 0; fi; "
                f"sleep {tick}; done; echo never; exit 1")

    def _wait_cmd(self, guest_cmd, timeout, tick):
        """Run a guest-side wait loop until it is satisfied or the deadline passes.

        rc 255 - ssh talking about itself - is not an answer about the
        condition, so it must not consume the wait; only the deadline does.
        Measured on ubuntu-gnome: after test_03's reboot the first look at the
        session came back 255 while the VM was still booting, and the suite
        reported "no session shell came back" about a machine that was fine.
        """
        deadline = time.monotonic() + timeout
        out = ""
        while True:
            left = max(1, int(deadline - time.monotonic()))
            rc, out = self.run(guest_cmd, timeout=max(30, left + 20))
            if rc != 255:
                return rc, out
            if time.monotonic() >= deadline:
                return 1, out or "ssh did not answer within the wait"
            time.sleep(0.5)

    def wait_ready(self, cursor, grep="traduku: ready", path="/tmp/traduku.log",
                   timeout=25.0, tick=0.2):
        """Wait for the app's own startup sentence, from either place it lands.

        Which place is a property of the session, not of the app: Qt hands a
        message to journald only when $JOURNAL_STREAM is in the environment. On
        arch-kde (ssh session that has it) the line reaches the journal even
        when fd 1 and 2 point at a file; on debian-kde (native X11 session from
        sddm, ssh session without it) the journal stays silent and only the
        file carries it - measured on both. Both texts are written by the
        product, so accepting either is not a weaker wait, and waiting for only
        one of them called a started app dead: a suite green on one guest
        turned into a false red on the next.

        start_app() does not need this - it launches the app as a systemd user
        unit, which always has JOURNAL_STREAM.
        """
        # One shell TEST, because _wait_loop wraps this in `if <test>; then`:
        # a compound if/then/else here is a syntax error, and a syntax error is
        # an rc that is not 255, so the wait would return "timed out" in 0.0 s.
        cmd = (f"journalctl --user --no-pager --after-cursor='{cursor}' 2>/dev/null"
               f" | grep -qi --color=never '{grep}'"
               f" || {{ [ -f {path} ] && grep -qi --color=never '{grep}' {path}; }}")
        return self._wait_cmd(self._ses_prefix() + self._wait_loop(cmd, timeout, tick),
                              timeout, tick)

    def wait_journal(self, cursor, grep, timeout=12.0, unit=None, tick=0.2):
        """Wait for a journal line after `cursor`. Returns (rc, out); rc 1 = timed out.

        For the startup sentence prefer wait_ready(): on a session where Qt has
        no JOURNAL_STREAM this waits for something that never arrives.
        """
        unit_arg = f" -u {unit}" if unit else ""
        return self._wait_cmd(self._ses_prefix() + self._wait_loop(
            f"journalctl --user --no-pager --after-cursor='{cursor}'{unit_arg} 2>/dev/null"
            f" | grep -qi --color=never '{grep}'", timeout, tick), timeout, tick)

    def wait_file(self, path, grep, timeout=12.0, tick=0.2):
        """Wait until a file in the guest contains `grep` (case-insensitive)."""
        return self._wait_cmd(self._wait_loop(
            f"grep -qi --color=never '{grep}' {path} 2>/dev/null", timeout, tick),
            timeout, tick)

    def wait_until(self, test_cmd, timeout=12.0, tick=0.3):
        """Wait for any shell test in the guest to become true.

        Takes a TEST, not a program name - `wait_until("gnome-shell")` does not
        ask whether a gnome-shell exists, it starts one (measured: a stray
        gnome-shell launched over ssh, and the tests after it died on ssh
        failures). If what you mean is "is this process running", use
        wait_process().
        """
        return self._wait_cmd(self._wait_loop(test_cmd, timeout, tick), timeout, tick)

    def wait_process(self, name, timeout=60.0, tick=0.5):
        """Wait for a live (non-zombie) process called `name` to exist."""
        return self._wait_cmd(self._alive_fn(name) + self._wait_loop("alive", timeout, tick),
                              timeout, tick)

    # ---- input (QMP scancodes: physical keys, layout-independent) ----
    FOCUS_TITLE = "traduku-e2e-focus"

    def focus_fresh_term(self, wait=10):
        """Put a foreign window on the screen, mapped and in front.

        Tests 07/08 must run with the app's own window NOT on top, so that the
        gesture they press really is the session's global shortcut and not
        something a window of ours handled. What this can and cannot promise,
        both measured on ubuntu-gnome:

        * `xdotool search -name xterm` matches nothing - the window is *named*
          "sleep" (what -e runs) and only *classed* "xterm" - so the command
          this replaced searched for a window that never existed, activated
          nothing, and its `sleep 1` was the entire setup. The window is found
          by a title this harness chose instead.
        * input focus cannot be read here: under GNOME's XWayland
          `XGetInputFocus` returns 1 (XInputFocusNone) for every window, so
          "did it take focus" is not an observable fact on this desktop. The
          activation is requested and the *mapping* is asserted - Map State:
          IsViewable - which is what the tests actually depend on.
        * leftovers are killed first: the terminal lives 300 s, so a second
          test used to find the first test's window. pkill/pgrep -f also match
          this ssh shell's own argv (it contains the pattern), so our pid is
          excluded - the trap mock-openai.py documents.

        Output goes to a log, not /dev/null: an unredirected background process
        holds the ssh channel open, and a harness that throws output away cannot
        debug the helper that failed.
        """
        title = self.FOCUS_TITLE
        rc, out = self.ses(
            'for p in $(pgrep -f "xterm -e sleep 300" 2>/dev/null; true); do '
            '[ "$p" != "$$" ] && kill "$p" 2>/dev/null; done; '
            f"setsid xterm -title {title} -e sleep 300 >/tmp/{title}.log 2>&1 & "
            f"end=$((SECONDS + {int(wait)})); id=; "
            "while [ $SECONDS -lt $end ]; do "
            f"id=$(xdotool search --name {title} 2>/dev/null | head -1); "
            '[ -n "$id" ] && break; sleep 0.2; done; '
            '[ -n "$id" ] && xdotool windowactivate $id 2>/dev/null; '
            'end=$((SECONDS + 5)); while [ $SECONDS -lt $end ]; do '
            '[ -n "$id" ] && xwininfo -id $id 2>/dev/null | grep -q "Map State: IsViewable" '
            "&& break; sleep 0.2; done; "
            'echo "window=$id map=$(xwininfo -id ${id:-0} 2>/dev/null '
            '| grep -o "Map State:.*")"')
        tail = out.strip().splitlines()[-1] if out.strip() else ""
        mapped = "IsViewable" in tail
        window = tail.split("window=", 1)[-1].split()[0].strip() if "window=" in tail else ""
        return mapped and window.isdigit(), out

    def clipget(self):
        # Both readers can block forever when the clipboard owner died
        # (ssh-owned wl-copy daemons die with the session, xclip exits with the
        # client that owned it); always bound them.
        if self.clip_tool("paste") == "x11":
            return self.ses("timeout 10 xclip -selection clipboard -o 2>/dev/null")[1]
        return self.ses("timeout 10 wl-paste 2>/dev/null")[1]

    def journal(self, since="--since -10min", grep="traduku"):
        return self.ses(f"journalctl --user --no-pager {since} | grep -i --color=never '{grep}' | tail -30")

    # ---- journal cursor: FRESH entries only (a test must never accept a
    # line produced by a previous test's gesture) ----
    def cursor(self):
        rc, out = self.ses("journalctl --user --show-cursor -n0 -o cat 2>/dev/null | tail -1")
        # last line: 'cursor: @19fa...'
        for line in out.strip().splitlines():
            line = line.removeprefix("-- ")
            if line.startswith("cursor: "):
                return line[len("cursor: "):]
        raise RuntimeError(f"no journal cursor in: {out!r}")

    def journal_after(self, cursor, grep="traduku"):
        # journalctl: --since and --after-cursor are mutually exclusive;
        # the cursor alone defines the read position.
        return self.ses(
            f"journalctl --user --no-pager --after-cursor='{cursor}'"
            f" | grep -i --color=never '{grep}' | tail -20")

    # ---- KWin window list via the scripting D-Bus API (Plasma 6:
    # loadScript + start; print() lands in kwin's journal stream) ----
    def kwin_windows(self, tag="gridwl"):
        js = f"""var s="{tag}: ";
for (const w of workspace.windowList()) {{
  try {{
    var cap = (w.caption || "").replace(/\\n/g, " ");
    s += "[" + cap + " " + w.width + "x" + w.height + " @" + w.x + "," + w.y
       + " min=" + w.minimized + " act=" + w.active + "] ";
  }} catch (e) {{ s += "[shell] "; }}
}}
print(s);
"""
        self.run(f"cat > /tmp/grid-winlist.js <<'JS'\n{js}JS")
        self.ses("gdbus call -e -d org.kde.KWin -o /Scripting"
                 " -m org.kde.kwin.Scripting.unloadScript gridwl >/dev/null 2>&1; true")
        self.ses("gdbus call -e -d org.kde.KWin -o /Scripting"
                 " -m org.kde.kwin.Scripting.loadScript /tmp/grid-winlist.js gridwl")
        cur = self.cursor()
        self.ses("gdbus call -e -d org.kde.KWin -o /Scripting"
                 " -m org.kde.kwin.Scripting.start")
        # The script prints its own tag; waiting for that line is both faster
        # than the 1.5 s it replaced and cannot return a list from the
        # previous call.
        self.wait_journal(cur, tag, timeout=10, tick=0.2)
        return self.journal_after(cur, grep=tag)[1]

    # ---- window list: three desktops, three truths (ROADMAP.md §4.1) -----
    def session_type(self):
        """wayland | x11 | '' — what the *graphical session* is, read from the
        env captured off the compositor, not from the app's QPA plugin."""
        rc, out = self.ses("echo \"$XDG_SESSION_TYPE\"")
        return out.strip().splitlines()[-1].strip().lower() if out.strip() else ""

    def has_kwin(self):
        return self.run("pgrep -x kwin_wayland || pgrep -x kwin_x11")[0] == 0

    def x11_windows(self, name="Traduku"):
        """The X server's own answer, per window: name, geometry, map state.

        Only meaningful on an X11 session. On Wayland a native client's
        window is not in the X server at all (an XWayland window list does not
        contain it), so this must never be the Wayland branch - it would read
        "no such window" while the window fills the screen.

        The map state is part of the witness on purpose. `xdotool search --name`
        also finds UNMAPPED windows, and our own process owns a 3x3 input-only
        "Qt Selection Owner for traduku" - so a name match alone can be a green
        over a window that was never shown. The tree dump at the end is what the
        compositor added (mutter's frame) and it is what a failure quotes, so a
        red here says which window existed and which did not.
        """
        return self.ses(
            f"for w in $(xdotool search --name '{name}' 2>/dev/null); do "
            f"i=$(xwininfo -id $w 2>/dev/null); "
            f"n=$(xdotool getwindowname $w 2>/dev/null); "
            f"W=$(echo \"$i\" | awk '/Width:/{{print $2}}'); "
            f"H=$(echo \"$i\" | awk '/Height:/{{print $2}}'); "
            f"M=$(echo \"$i\" | awk '/Map State:/{{print $3}}'); "
            f"C=$(xprop -id $w WM_CLASS 2>/dev/null | grep -o '\"[^\"]*\"' | tail -1 | tr -d '\"'); "
            f"echo \"win $w name=$n class=$C geom=${{W}}x${{H}} map=$M\"; done; "
            f"xwininfo -root -tree 2>/dev/null | grep -i '{name}' | head -8"
        )[1]

    def window_evidence(self, name="Traduku", cursor=None):
        """(how, text): how THIS desktop proves a window is on screen.

        The old test knew only KWin, which is why it was red on GNOME for a
        reason unrelated to the product. What each desktop can answer:
        Plasma -> KWin's scripting window list (caption, frame size, mapped)
        X11    -> the X server (xdotool)
        Wayland (no KWin) -> neither exists: a client may not enumerate other
                  clients' windows and gnome-shell has no scripting API, so
                  the app's own `window shown WxH` journal line is the witness,
                  paired with the QMP framebuffer diff the caller took.
        """
        if self.has_kwin():
            return "kwin", self.kwin_windows()
        if self.session_type() == "x11":
            return "xdotool", self.x11_windows(name)
        if cursor is None:
            raise RuntimeError("wayland window evidence needs a journal cursor")
        return "app journal", self.journal_after(cursor, grep="window shown")[1]

    def keyseq(self, seq):
        self.vmctl("keyseq", self.vm, seq)

    def mouse_click(self, x, y, button="left"):
        self.vmctl("mouse", self.vm, "click", str(x), str(y), button)

    def mouse_move(self, x, y):
        self.vmctl("mouse", self.vm, "move", str(x), str(y))

    def double_ctrl_c(self):
        self.keyseq("ctrl c;200;ctrl c")

    def double_ctrl_alt_c(self):
        self.keyseq("ctrl alt c;200;ctrl alt c")

    # ---- framebuffer (QMP screendump: compositor independent) ----
    def shot(self, name):
        self.shots.mkdir(parents=True, exist_ok=True)
        (self.shots / f"{name}.ppm").unlink(missing_ok=True)
        self.vmctl("shot", self.vm, name)
        p = self.shots / f"{name}.ppm"
        for _ in range(10):
            if p.exists() and p.stat().st_size > 0:
                return p
            time.sleep(0.2)
        raise RuntimeError(f"screendump {name} produced nothing")

    def diff_frac(self, a, b):
        """Fraction of the framebuffer that changed between two shots."""
        out = subprocess.run(
            ["docker", "exec", "-i", "traduku-pipeline-vmm", "python3", "-"]
            , input=f"""
from PIL import Image, ImageChops
a=Image.open(r"/vms/run/{self.vm}/shots/{a}.ppm").convert("RGB")
b=Image.open(r"/vms/run/{self.vm}/shots/{b}.ppm").convert("RGB")
bbox=ImageChops.difference(a,b).getbbox()
area=(bbox[2]-bbox[0])*(bbox[3]-bbox[1]) if bbox else 0
print(frac:=area/(a.size[0]*a.size[1]))
""".strip(),
            capture_output=True, text=True).stdout.strip()
        return float(out.splitlines()[-1])


def close_all():
    """Close every multiplexed connection (pytest_sessionfinish)."""
    for grid in list(_GRIDS):
        grid.close_master()
        _GRIDS.remove(grid)
