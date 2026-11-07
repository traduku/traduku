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


class Grid:
    def __init__(self, vm):
        self.vm = vm
        idx = self._idx()
        self.port = 2200 + idx
        self.shots = VMS_HOME / "run" / vm / "shots"

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
    def _ssh_cmd(self, cmd):
        return ["ssh", "-i", str(VMS_HOME / "id_ed25519"),
                "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                "-o", "ConnectTimeout=10", "-o", "ServerAliveInterval=15",
                "-p", str(self.port), "vm@127.0.0.1", cmd]

    def run(self, cmd, timeout=120):
        """Run in the guest; returns (rc, stdout)."""
        p = subprocess.run(self._ssh_cmd(cmd), capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout

    def ok(self, cmd, timeout=120):
        rc, out = self.run(cmd, timeout)
        return rc == 0, out

    def push(self, local, remote):
        subprocess.run(["scp", "-i", str(VMS_HOME / "id_ed25519"),
                        "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                        "-P", str(self.port), str(local), f"vm@127.0.0.1:{remote}"],
                       check=True, capture_output=True)

    def pull(self, remote, local):
        subprocess.run(["scp", "-i", str(VMS_HOME / "id_ed25519"),
                        "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                        "-P", str(self.port), f"vm@127.0.0.1:{remote}", str(local)],
                       check=True, capture_output=True)

    # ---- session ----
    def wait_ssh(self, seconds=420):
        end = time.time() + seconds
        while time.time() < end:
            try:
                rc, _ = self.run("echo up", timeout=15)
                if rc == 0:
                    return True
            except subprocess.TimeoutExpired:
                pass
            time.sleep(5)
        return False

    def sudo(self, cmd):
        return self.run(f"echo vm | sudo -S {cmd}")

    def clipset(self, text):
        """Seed the DE (Wayland) clipboard. wl-copy is a daemon holding
        stdout — redirect it, ssh would block on the open channel."""
        return self.ses(f"printf %s '{text}' | timeout 10 wl-copy >/dev/null 2>&1")

    def start_app(self):
        """(Re)launch the packaged app as a transient user unit.

        systemd-run, NOT `setsid nohup >file`: journal_after() (gesture
        freshness in tests 05/06 and the KGA warm-up gate) reads
        journalctl, and a file-redirected app silently loses every
        'translating' line there - arch only ever looked green because
        the autostart-unit instance (journal-captured) also runs in the
        session and produced the lines. One unit, both distros, one
        logging truth. --collect removes the unit on exit so the next
        start is clean; --setenv forwards the session trio captured by
        capture_sessionenv.
        """
        self.ses("systemctl --user stop traduku-e2e.service >/dev/null 2>&1; true")
        self.ses("pkill -x traduku; sleep 1; true")
        # full session environment through the wrapper: KWindowSystem /
        # the KGA backends branch on XDG_SESSION_TYPE/XDG_CURRENT_DESKTOP,
        # --setenv of the display trio alone silently picked the wrong
        # (x11) path on arch and global shortcuts stopped arriving.
        self.ses(
            "systemd-run --user --collect --unit=traduku-e2e.service "
            "sh -c '. /tmp/session.env; exec /usr/bin/traduku'")

    def ses(self, script, timeout=120):
        """Run a snippet inside the captured graphical session."""
        return self.run(f"source /tmp/session.env; {script}", timeout=timeout)

    # ---- input (QMP scancodes: physical keys, layout-independent) ----
    def clipget(self):
        # wl-paste can block forever when the clipboard owner died (ssh-owned
        # wl-copy daemons die with the session); always bound it.
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
        self.ses("gdbus call -e -d org.kde.KWin -o /Scripting"
                 " -m org.kde.kwin.Scripting.start")
        time.sleep(1.5)
        return self.journal("--since -10s", grep=tag)[1]

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
            time.sleep(0.5)
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
