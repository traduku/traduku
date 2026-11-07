"""Session fixtures for the traduku GUI grid tests.

TRADUKU_VM selects the grid VM (default arch-kde — the program's native
environment, KDE Plasma Wayland, see ROADMAP.md §6).
"""
import os
import re
import time

import pytest

from grid import Grid


@pytest.fixture(scope="session")
def vm():
    g = Grid(os.environ.get("TRADUKU_VM", "arch-kde"))
    assert g.wait_ssh(), f"{g.vm}: ssh never came up (is the VM started?)"
    return g


def capture_sessionenv(vm):
    """Refresh /tmp/session.env from the running compositor.

    KWin is (re)startable per test (xkb layout changes need it), and a
    restarted KWin publishes a new wayland socket number — the socket is
    discovered from the runtime dir, never from kwin's own environ.
    """
    vm.run(
        "P=$(pgrep -u vm -x kwin_wayland | head -1); [ -z \"$P\" ] && P=$(pgrep -u vm -x kwin_x11 | head -1); [ -z \"$P\" ] && P=$(pgrep -u vm -x gnome-shell | head -1);"
        " echo vm | sudo -S cat /proc/$P/environ | tr '\\0' '\\n'"
        " | grep -E '^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_RUNTIME_DIR|XDG_SESSION_TYPE|XDG_DATA_DIRS|XDG_CURRENT_DESKTOP)='"
        " | sed 's/^/export /' > /tmp/session.env;"
        # XDG_DATA_DIRS belongs to the compositor, not to the ssh shell: an ssh
        # session has none, and without it KNotification cannot find
        # /usr/share/knotifications6/traduku.notifyrc - the app then logs
        # "No event config could be found" and the notification asserts test a
        # deliberately impoverished environment instead of the desktop.
        " D=$(echo vm | sudo -S cat /proc/$P/environ | tr '\\0' '\\n' | grep -E '^XDG_DATA_DIRS=' | sed 's/^/export /');"
        " [ -n \"$D\" ] && echo \"$D\" >> /tmp/session.env;"
        " grep -q XDG_DATA_DIRS /tmp/session.env || echo 'export XDG_DATA_DIRS=/usr/local/share:/usr/share' >> /tmp/session.env;"
        # the compositor publishes its socket under the runtime dir — trust
        # the socket (kwin does not put WAYLAND_DISPLAY in its own environ)
        " W=$(ls /run/user/1000/ 2>/dev/null | grep -E '^wayland-[0-9]+$' | head -1);"
        " [ -n \"$W\" ] && echo \"export WAYLAND_DISPLAY=$W\" >> /tmp/session.env;"
        # XWayland (KDE: screenless, :1) provides DISPLAY/XAUTHORITY from its env
        " X=$(echo vm | sudo -S cat /proc/$(pgrep -u vm -x Xwayland | head -1)/environ 2>/dev/null"
        " | tr '\\0' '\\n' | grep -E '^(DISPLAY|XAUTHORITY)=' | sed 's/^/export /');"
        " [ -n \"$X\" ] && echo \"$X\" >> /tmp/session.env;"
        " grep -q XDG_RUNTIME_DIR /tmp/session.env || echo 'export XDG_RUNTIME_DIR=/run/user/1000' >> /tmp/session.env"
    )
    # Read it back. /tmp is wiped by a reboot, and a session.env that is empty
    # or absent turns every later `source /tmp/session.env` into a mystery
    # ("traduku never reported ready", "ssh: cannot open file") whose cause sits
    # three commands earlier. This names it where it happens.
    rc, back = vm.run("wc -c < /tmp/session.env 2>/dev/null; grep -c DBUS_SESSION_BUS_ADDRESS /tmp/session.env 2>/dev/null")
    lines = back.split()
    if rc != 0 or len(lines) < 2 or lines[0] == "0" or lines[1] == "0":
        raise RuntimeError(
            "the graphical session gave no environment to capture: /tmp/session.env "
            f"is {lines[0] if lines else 'missing'} bytes with {lines[1] if len(lines) > 1 else 'no'} "
            "bus address. Nothing is logged in on the profile yet (or /tmp was just "
            "wiped by a reboot and the session has not come back): " + back)


def sni_watcher_up(vm):
    """Does a StatusNotifier host answer on this session bus?

    Both hosts use the same well-known name (Plasma's own tray, and the
    appindicator extension on GNOME), so this is the one question that says
    whether a panel icon is possible at all. `IsStatusNotifierHostRegistered`
    is checked too: a watcher that nobody hosts would list items forever and
    never draw one.
    """
    rc, _ = vm.ses(
        "gdbus call --session -d org.kde.StatusNotifierWatcher"
        " -o /StatusNotifierWatcher -m org.freedesktop.DBus.Introspectable.Introspect")
    if rc != 0:
        return False, "no org.kde.StatusNotifierWatcher on the session bus"
    rc, out = vm.ses(
        "gdbus call --session -d org.kde.StatusNotifierWatcher"
        " -o /StatusNotifierWatcher -m org.freedesktop.DBus.Properties.Get"
        " org.kde.StatusNotifierWatcher IsStatusNotifierHostRegistered")
    if rc != 0 or "true" not in out:
        return False, f"watcher exists but hosts no tray (IsStatusNotifierHostRegistered={out.strip()!r})"
    return True, ""


def ensure_sni_host(vm):
    """Make the panel able to show an SNI item, or say exactly why it cannot.

    GNOME ships no StatusNotifier host of its own: the icon exists only with
    the appindicator extension (ROADMAP.md §2 - which is why the .deb only
    `Suggests:` it). mkvm.sh enables it for the vm user through a dconf
    default; this covers a guest built before that commit. Enabling is not
    enough by itself - gnome-shell scans the extension directories at boot,
    so `gnome-extensions enable` on a shell that never saw the directory
    reports "Extension does not exist" (measured) and the reason returned
    here names the missing reboot. test_04 skips with that reason: a tray
    test that asserts nothing is worse than a red one.
    """
    up, why = sni_watcher_up(vm)
    if up:
        return None
    if not vm.vm.endswith("-gnome"):
        return f"no StatusNotifier host in this session ({why}) - Plasma's tray is the host"
    rc, out = vm.run(
        "ls -d /usr/share/gnome-shell/extensions/*appindicator* "
        "$HOME/.local/share/gnome-shell/extensions/*appindicator* 2>/dev/null")
    uuids = [p.strip().rstrip("/").rsplit("/", 1)[-1] for p in out.splitlines() if p.strip()]
    if not uuids:
        return ("no StatusNotifier host and no appindicator extension installed "
                f"({why}); traduku-gnome only Suggests gnome-shell-extension-appindicator "
                "because the app translates without an icon")
    uuid = uuids[0]
    rc, cur = vm.ses("gsettings get org.gnome.shell enabled-extensions")
    enabled = re.findall(r"'([^']+)'", cur)
    changed = uuid not in enabled
    if changed:
        merged = ", ".join("'%s'" % u for u in enabled + [uuid])
        vm.ses(f'gsettings set org.gnome.shell enabled-extensions "[{merged}]"')
        vm.ses(f"gnome-extensions enable {uuid} 2>&1 || true")
        for _ in range(10):
            time.sleep(2)
            up, why = sni_watcher_up(vm)
            if up:
                return None
    return (f"appindicator extension {uuid} is installed"
            + (" and was just enabled for the user" if changed else " and enabled in gsettings")
            + f", but the running gnome-shell has no SNI host ({why}); the shell only "
              "scans extension directories at boot - recreate the image "
              "(tools/vms/mkvm.sh) or reboot the guest")


def _prepare_gnome_session(vm):   # plain helper, not a fixture: `session` calls it
    """GNOME prep, because the KDE prep is not desktop-neutral.

    It bakes a wallpaper through KWin's scripting D-Bus, silences the idle
    lock with kwriteconfig6 and bounces plasma-kglobalaccel - none of which
    exists on GNOME. The first attempt to run the suite on a gnome profile
    died in this fixture on `pgrep kwin_wayland`, which is how a "gnome"
    column in the grid had never actually executed a test.
    """
    rc, out = vm.run("pgrep -u vm -x gnome-shell")
    assert rc == 0, "no GNOME session in the guest (gdm autologin broken?)"
    capture_sessionenv(vm)
    vm.sudo("loginctl unlock-sessions")
    # The idle lock is the same noise source as on Plasma. The guest image
    # sets these through dconf at build time; assert them instead of trusting
    # the image - a locked screen mid-test swallows the window and every
    # screenshot assert below it dies for an unrelated reason.
    vm.ses("gsettings set org.gnome.desktop.screensaver lock-enabled false; "
           "gsettings set org.gnome.desktop.session idle-delay 0; "
           "gsettings get org.gnome.desktop.screensaver lock-enabled")


@pytest.fixture(scope="session")
def session(vm):
    """Prepare the graphical session: env capture, no idle-lock, mock LLM, config."""
    if vm.vm.endswith("-gnome"):
        # Its own preparation; see _prepare_gnome_session.
        _prepare_gnome_session(vm)
        capture_sessionenv(vm)
        vm.run("rm -f ~/.config/traduku/config.ini; pkill -x traduku; true")
        return vm
    rc, out = vm.run("pgrep -u vm -x kwin_wayland || pgrep -u vm -x kwin_x11")
    assert rc == 0, "no KDE session in the guest (autologin broken?)"
    capture_sessionenv(vm)
    vm.sudo("loginctl unlock-sessions")
    # Deterministic pixels for framebuffer diffs: bake a static color
    # background (the session fixture also kills the real noise source -
    # the idle lock screen, see below).
    vm.ses("""cat > /tmp/grid-wall.js <<'JS'
var d = desktops[0];
d.plugin = "color";
d.currentConfigGroup = ["Background"];
d.writeConfig("color", "#2b3e50ff");
d.currentConfigGroup = [];
JS
gdbus call -e -d org.kde.plasmashell -o /PlasmaShell -m org.kde.PlasmaShell.evaluateScript "$(cat /tmp/grid-wall.js)"
""")
    # No idle lock, no DPMS during the suite (observed: lock screen at +10min
    # idle swallowed the window mid-test). kwin reads kwinidlerc at startup.
    vm.ses("kwriteconfig6 --file kwinidlerc --group ScreenLocker --key Enabled false; "
           "kwriteconfig6 --file kwinidlerc --group DPMS --key Enabled false; "
           # X11 keyboard access on KWin 6.8 - measured facts, not folklore:
           # the EIS pop-up/allowlist (XwaylandEisNoPrompt*) gates keyboard
           # GRABS (XGrabKeyboard), and the current traduku (passive XI2
           # raw, no grabs) never triggers it - the VM's allowlist stays
           # empty on purpose. XI2 raw on rootless XWayland is structurally
           # empty: KWin injects keys as send-events, which the raw stream
           # filters out (xinput --root = 0 RAWKEY for QMP scancodes while
           # xterm next door takes the SIGINT). Plain Ctrl+C+C therefore
           # has no listener on Wayland; the gesture is exercised via the
           # KGA shortcut pair (tests 05/07/08). Native XI2 coverage is an
           # X11-session job (debian-kde, planned).
           "systemctl --user stop plasma-powerdevil.service; true")
    vm.ses("pkill -x powerdevil; true")
    # Bounce kwin so kwinidlerc takes effect (detached: ssh would block on
    # the D-Bus job; KGA bounce keeps shortcut registrations clean, cf.
    # restart_kwin notes). Plasma 6.8 (arch-kde) only: on Debian's Plasma
    # 6.3 the session is started by sddm/startplasma-wayland, NOT by the
    # user unit, so restarting plasma-kwin_wayland.service can never
    # resurrect the compositor - the unit's wrapper picks its backend from
    # the environment (nested wayland/x11), the DRM master stays held by
    # the dying instance, and the whole session deadlocks (measured:
    # plasmashell/kded units time out, orphaned /tmp/.X11-unix/X0, every
    # wl/X client hangs). Bouncing KGA alone is safe and is all the idle
    # guards need here (powerdevil is stopped outright, kwinidlerc is a
    # Plasma 6.4+ file). Same rule applies to restart_kwin (test_05):
    # layout-reload-by-restart is arch-kde-only for now.
    # KGA only, on every Plasma image. Bouncing KWin alongside it used to be
    # arch-only and it cost the run its X server: measured on arch-kde, after
    # `systemctl --user restart plasma-kwin_wayland.service` no Xwayland exists
    # for the rest of the session - the killed one leaves /tmp/.X0-lock and a
    # stale /tmp/.X11-unix/X0, and the replacement refuses to start ("Server is
    # already active for display 0"). Everything X-side then hangs to its
    # timeout: the layout phase, the key listener, the app start. The restart
    # was protecting nothing here - kwinidlerc matters for idle guards, which
    # powerdevil also covers, and the guards cannot fire inside a 60 s suite.
    vm.ses("setsid sh -c 'systemctl --user restart plasma-kglobalaccel.service' >/dev/null 2>&1 &")
    rc, _ = vm.wait_until(
        "ls /run/user/1000/ | grep -qE '^wayland-[0-9]+$' && pgrep -u vm -x kwin_wayland",
        timeout=45, tick=0.5)
    assert rc == 0, "KWin did not come back after the bounce"
    # env/app starts must not race the bounced KGA (see restart_kwin note).
    # Introspect answering IS the readiness probe; the two extra seconds that
    # used to follow it protected nothing, and _gesture_is_alive below is the
    # claim that actually has to hold.
    rc, _ = vm.wait_until(
        "gdbus call -e -d org.kde.kglobalaccel -o /kglobalaccel"
        " -m org.freedesktop.DBus.Introspectable.Introspect", timeout=30, tick=0.3)
    assert rc == 0, "kglobalaccel never answered after the bounce"
    capture_sessionenv(vm)
    vm.run("rm -f ~/.config/traduku/config.ini; pkill -x traduku; true")
    return vm


def ses(vm, script, timeout=120):
    """Run a snippet inside the graphical session."""
    return vm.run(f"source /tmp/session.env; {script}", timeout=timeout)


def start_mock(vm):
    """(Re)start the mock LLM endpoint. Must run per-test, not per-session:
    test_03 REBOOTS the VM and the mock dies with it (observed: every
    post-reboot translation hit a dead 8931 -> Connection refused ->
    translating logged, no MOCK, tests 5/7/8 red for the wrong reason).
    """
    health = "curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8931/v1/models"
    if "200" in vm.run(health)[1]:
        return                     # already serving; nothing to do
    from grid import ROOT
    vm.push(ROOT / "tools" / "mock-openai.py", "~/mock-openai.py")
    # pkill/pgrep -f also match OUR OWN ssh shell argv (it contains the
    # script path); exclude our pid — only the real server dies.
    vm.run("for p in $(pgrep -f mock-openai.py 2>/dev/null; true); do "
           "[ \"$p\" != \"$$\" ] && kill \"$p\" 2>/dev/null; done; "
           "setsid nohup python3 /home/vm/mock-openai.py 8931 >/tmp/mock.log 2>&1 </dev/null &")
    # The endpoint answers when it answers: ask it, in the guest, at 0.2 s
    # granularity. `sleep 0.5` in the guest plus `sleep 1` here used to be the
    # substitute, and this ran before every test - 1.5 s x 22 of a 5-minute
    # suite, on a server that was already up 21 times out of 22.
    rc, out = vm.wait_until(health + " | grep -q 200", timeout=15, tick=0.2)
    assert rc == 0, "mock endpoint is not serving:\n" + vm.run("cat /tmp/mock.log")[1]


@pytest.fixture(scope="session")
def env(session):
    """Live GUI environment: mock LLM endpoint, config, app running in-session.

    The app is the *installed* binary; the mock streams `[MOCK -> lang] <text>`.
    """
    vm = session
    start_mock(vm)

    write_mock_config(vm)

    if vm.run("command -v traduku")[0] != 0:
        # Platform without an installable package yet (e.g. debian-kde
        # before the .deb phase): start nothing and assert nothing here -
        # the per-test body asserts (tray/journal/clipboard) will fail
        # with the real reason instead of every test erroring identically
        # in the fixture.
        return vm
    # start_app() returns when the app has said "traduku: ready"; the old
    # sleep 4 + pgrep only ever waited for the clock and then trusted it.
    vm.start_app()
    return vm


def write_mock_config(vm):
    """The harness config: mock endpoint, pair interval, CopyResult.

    MUST be written while the app is stopped: the app rewrites
    config.ini on exit (observed: test_02's first-run flow exited the
    default-config app -> config.ini = defaults -> the rebooted autostart
    app talked to localhost:11434, every MOCK-dependent assert died).
    """
    vm.run("mkdir -p ~/.config/traduku; cat > ~/.config/traduku/config.ini <<'INI'\n"
           "[General]\nApiUrl=http://127.0.0.1:8931/v1\nModel=mock\n"
           "TargetLanguage=en\nPressInterval=0.8\nSoundsEnabled=false\n"
           "CopyResult=true\nFirstRunDone=true\nINI\n"
           "grep -c ApiUrl ~/.config/traduku/config.ini")


@pytest.fixture(autouse=True)
def app_running(env):
    """Every GUI test starts from a fresh app instance on harness config.

    A restart (not a liveness check) isolates the double-press detector:
    key events from a previous/interrupted test must never pair with this
    test's presses (observed: an interrupted run's keyseq polluted the
    next single-press assertion).

    The config is re-baked after EVERY stop, so a test that manipulated
    it (first-run, CopyResult=false red-check) cannot leak into the next.
    """
    capture_sessionenv(env)  # reboot/kwin restart rotates WAYLAND_DISPLAY/xauth
    start_mock(env)          # test_03 reboots the VM; the mock must come back
    write_mock_config(env)
    if env.run("command -v traduku")[0] != 0:
        return env           # see the env-fixture note (uninstalled platform)
    # This one call stops the previous instance, starts the unit and waits for
    # the app's own "traduku: ready" line. It used to be `pkill; sleep 1` here
    # plus sleep 4 + sleep 1 around it: six seconds of guessing in every test -
    # 130 s of the suite, and still flaky on a busy VM. Waiting for the
    # sentence is both faster and stronger: a startup that wedges now fails
    # naming the backend that never came up, instead of 4 s later with a
    # screenshot of nobody's window.
    env.start_app()
    # Close whatever shell chrome a previous test left open. Observed on
    # ubuntu-gnome after the layout switching of test_05: GNOME was still in
    # the Activities overview, so the app mapped its window into a workspace
    # thumbnail and test_06 reported "screen changed by only 0.000" for a
    # product that had translated correctly (the journal line proved it).
    env.keyseq("esc")
    return env


def set_layouts(vm, layouts):
    """Give the session two switchable layouts - the way THIS desktop does it.

    test_05 has to press the same physical keys under another layout. Each
    desktop applies layouts its own way and neither way is portable: GNOME
    reads org.gnome.desktop.input-sources live, Plasma's X side is setxkbmap.
    The recipe that used to be run everywhere - write kxkbrc and restart the
    compositor - is not merely useless on GNOME (no kwriteconfig6, no KGA), it
    burned two minutes failing and left gnome-shell in the Activities overview,
    which the framebuffer diffs of the tests below then reported as "the window
    never appeared". Both branches now end only when the gesture's own round
    trip worked, see _gesture_is_alive.
    """
    """Returns (method, reason): "live" when the session demonstrably carries
    the layouts and the gesture round trip worked, or (None, why) naming the
    machine fact that says otherwise. The reason is a return value, not module
    state: pytest loads this file twice (as `conftest` for the tests' imports
    and as `tests.e2e.conftest` for the fixtures), so a global holding the
    "why" was written in one copy and read from the other - the skip came out
    with the fallback wording and the measurement was lost (measured).
    """
    method, reason = _gnome_set_layouts(vm, layouts) if vm.vm.endswith("-gnome") \
        else _plasma_set_layouts(vm, layouts)
    if method is None:
        return None, reason
    rc, out = vm.run("command -v traduku")
    if rc != 0:
        return None, f"no traduku binary on {vm.vm} (rc={rc}: {out.strip()})"
    alive, reason = _gesture_is_alive(vm)
    if not alive:
        return None, reason
    return method, ""


def _gnome_set_layouts(vm, layouts):
    """org.gnome.desktop.input-sources is read live by gnome-settings-daemon.

    No compositor bounce - and that is the point: bouncing gnome-shell here
    would take the session, and every test after it, down with it. Alt+Shift
    comes from the xkb option, the same one the Plasma branch writes, because
    test_05 switches layouts by pressing exactly that.
    """
    entries = ", ".join("('xkb', '%s')" % one.strip() for one in layouts.split(","))
    vm.ses("gsettings set org.gnome.desktop.input-sources sources \"[" + entries + "]\";"
           " gsettings set org.gnome.desktop.input-sources xkb-options"
           " \"['grp:alt_shift_toggle']\"")
    rc, out = vm.ses("gsettings get org.gnome.desktop.input-sources sources")
    for one in [x.strip() for x in layouts.split(",")]:
        assert ("'%s'" % one) in out, f"gnome-shell did not take the {one} layout: {out}"
    capture_sessionenv(vm)
    return "live", ""        # gnome-settings-daemon feeds the group to XWayland


def _x_session(vm):
    """(usable, evidence) - whether an X server here answers a client, asked
    of the product rather than inferred from how long a socket stays silent.

    `TRADUKU_CLIPBOARD=x11 traduku --backend-info` makes the app do exactly
    what this suite needs it to do anyway: find the X server of a Wayland
    session (X11Session::resolveDisplay - the socket plus the auth cookie that
    a systemd/ssh process never inherits) and either name x11 as the backend it
    chose or refuse and *say why* (X11Clipboard::unusabilityReason). Asking it
    gives a condition and an explanation in one read; polling a socket for
    seconds gives a duration, and a duration is not a reason to skip anything.

    The forced backend cannot hurt the running app: this is a separate CLI
    process, and the knob only decides which reader that process would use.
    """
    rc, out = vm.ses("TRADUKU_CLIPBOARD=x11 traduku --backend-info 2>&1")
    lines = [l.strip() for l in out.splitlines()]
    for l in lines:
        if l.lower().startswith("clipboard:") and " x11" in (" " + l.lower()):
            return True, l
    for l in lines:
        if "hint" in l.lower() and "clipboard" in l.lower():
            return False, l
    return False, "the app named no x11 clipboard and gave no reason: " + " | ".join(lines[:6])


def _plasma_set_layouts(vm, layouts):
    """Two layouts on a Plasma session, live, without touching the compositor.

    Returns "live" once the X server demonstrably carries both layouts, None
    only when the machine itself reports that it has no X to work with - which
    is a condition read from the product, not a wait that ran out. If the
    product says the X server is usable, everything below it that fails is a
    failure: on a machine that can do the job, the job has to be done.

    The recipe this replaces wrote kxkbrc and restarted
    plasma-kwin_wayland.service. Measured on arch-kde: after that restart there
    is no Xwayland process for the rest of the session - the killed one leaves
    /tmp/.X0-lock and a stale /tmp/.X11-unix/X0 behind, and the replacement
    refuses to start ("Server is already active for display 0"), so every X
    client of every later test loses the session's keyboard. What the restart
    bought was the kxkb layout list, which this test does not read: the keysyms
    of injected presses come from the X server's own xkb map, and setxkbmap sets
    that directly. kxkbrc is still written - a session a *user* starts later
    must carry the layouts too.
    """
    vm.ses(" ".join([
        f"kwriteconfig6 --file kxkbrc --group Layout --key LayoutList '{layouts}'",
        "kwriteconfig6 --file kxkbrc --group Layout --key SwitchMode Keyboard",
        "kwriteconfig6 --file kxkbrc --group Layout --key Options grp:alt_shift_toggle",
        "kwriteconfig6 --file kxkbrc --group Layout --key Initial 0"]))
    usable, evidence = _x_session(vm)
    if not usable:
        return None, f"the app itself reports no usable X server: {evidence}"
    # From here the X server is a promise the product made, so these are
    # assertions. `timeout 4` bounds a request that could otherwise hang the
    # ssh channel forever on a wedged server; it is a leash, not a wait.
    rc, out = vm.ses(f"timeout 4 setxkbmap -layout {layouts} -option grp:alt_shift_toggle")
    assert rc == 0, (f"{evidence}, yet setxkbmap could not apply {layouts}: "
                     f"rc={rc} {out.strip()}")
    rc, out = vm.ses("timeout 4 setxkbmap -query")
    line = " ".join(l for l in out.splitlines() if "layout" in l)
    for one in [x.strip() for x in layouts.split(",")]:
        assert one in line, (f"the X server does not carry the {one} layout it was "
                             f"given: {line!r} (setxkbmap -query: {out.strip()})")
    # KGlobalAccel must be up before the gesture is pressed: a registration
    # made while the daemon is starting is dropped silently (observed
    # in-suite: the pair gesture dead for the whole test, alive minutes later).
    rc, _ = vm.wait_until(
        "gdbus call -e -d org.kde.kglobalaccel -o /kglobalaccel"
        " -m org.freedesktop.DBus.Introspectable.Introspect", timeout=30, tick=0.3)
    assert rc == 0, "kglobalaccel did not answer, so the shortcut cannot be registered"
    capture_sessionenv(vm)
    return "live", ""


def clip(vm):
    """What the desktop clipboard holds, read the way the assertions read it.

    One read path on purpose: a `wl-paste | grep -q X` inside the guest and a
    separate `wl-paste` for the assert are two reads, and on arch-kde they
    disagreed for 8 s while the clipboard carried the text the assert found
    immediately.
    """
    return vm.ses("wl-paste 2>/dev/null")[1]


def clip_until(vm, needle, timeout=6, tick=0.3):
    """Wait until the desktop clipboard carries `needle`; (ok, what was seen)."""
    deadline = time.time() + timeout
    seen = ""
    while time.time() < deadline:
        seen = clip(vm)
        if needle in seen:
            return True, seen
        time.sleep(tick)
    return False, seen


def select_layout(vm, code):
    """Put the session into ONE keyboard layout and prove it from the
    desktop's own answer. Returns (ok, reason).

    This replaces "press Alt+Shift and hope the group toggled, then read the
    keysym the product's X listener reports". That probe cannot work on a
    Wayland session and does not need to: keys injected into the guest arrive
    at XWayland as send-events, which X RECORD and XI2 raw do not report (the
    same reason plain Ctrl+C+C has no X listener on Wayland - documented in
    _enable_kga), so the listener is blind to exactly the presses this suite
    makes. What the test actually promises is "the same physical keys fire the
    gesture while the session is russian", and the state it depends on is
    something the desktop will state outright: setxkbmap reports the layout it
    applies, gnome-settings-daemon reports its input sources.

    Selecting one layout rather than toggling between two is also what the
    claim needs - a group indicator nobody can read proves nothing, a session
    whose sources are only `ru` is russian.
    """
    if vm.vm.endswith("-gnome"):
        vm.ses("gsettings set org.gnome.desktop.input-sources sources"
               " \"[('xkb', '%s')]\"" % code)
        rc, out = vm.ses("gsettings get org.gnome.desktop.input-sources sources")
        if code not in out:
            return False, f"gnome-shell did not select the {code} layout: {out.strip()}"
        return True, ""
    rc, out = vm.ses(f"timeout 4 setxkbmap -layout {code}")
    if rc != 0:
        return False, f"setxkbmap refused the {code} layout: rc={rc} {out.strip()}"
    rc, out = vm.ses("timeout 4 setxkbmap -query")
    line = " ".join(l for l in out.splitlines() if "layout" in l)
    if code not in line:
        return False, f"the X server does not report the {code} layout: {line!r}"
    return True, ""


def _gesture_is_alive(vm):
    """Fire the pair gesture until the whole round trip is visible.

    Returns (ok, reason): True when both halves were seen, False with the
    sentence that says which machine fact stopped it. Two halves are waited for, because the gesture has two
    halves and a readiness gate that checks one green-lights a suite whose
    clipboard never changes:

      1. the app's own "traduku: translating ... 8931" journal line for THIS
         press (a cursor taken right before it, so no earlier press can be
         credited to this one);
      2. the translation arriving in the clipboard.

    Half 2 needs a condition the harness has to provide, and that is why this
    function starts a terminal: the write-back goes through wl-clipboard, whose
    X11 fallback can own CLIPBOARD only while an X11 window has the focus. With
    the app's own Wayland window in front the translation is logged and the
    clipboard never changes (measured on arch-kde - "translating 20 chars ->
    English (mock)" in the journal, `wl-paste` unchanged for 10 s). Leaving that
    write queued does not make it disappear either: it landed seconds later, on
    top of the seed the next test had just written, and test_05 reported a
    single press as having translated. Focusing a foreign window is also simply
    the situation a user is in - text selected somewhere else - and is what
    tests 07/08 do for the same reason.

    On Plasma 6 kglobalaccel lives INSIDE kwin_wayland: a kwin restart drops
    the app's registration and the client library re-registers only when the
    bus name reappears - on a young session that lag exceeds any sane sleep
    (observed: gesture dead for the whole test, alive ~30 s later), so this
    probes the real path, by pressing it.
    """
    rc, out = vm.run("command -v traduku")
    if rc != 0:
        return True, ""      # nothing registered: the test body fails on its
                             # own asserts with the real (no-app) reason
    vm.focus_fresh_term()    # the focus the write-back needs, see above
    deadline = time.time() + 90
    attempt = 0
    writes = 0
    while time.time() < deadline:
        cur = vm.cursor()
        vm.clipset(f"set_layouts warmup {attempt}")
        vm.double_ctrl_alt_c()
        # 0.2 s resolution: the line appears the moment the request reaches the
        # mock, and a 1 s poll mostly overslept it. The 0.5 s between presses
        # separates two gestures; it is not a stand-in for evidence.
        rc, _ = vm.wait_journal(cur, "translating.*8931", timeout=4, tick=0.2)
        if rc != 0:
            attempt += 1
            time.sleep(0.5)
            continue
        ok, seen = clip_until(vm, "MOCK", timeout=10)
        if ok:
            return True, ""
        writes += 1
        if writes >= 3:
            return False, (
                "the gesture reaches the app (its own log says so) but the "
                "translation never arrives in the clipboard - three presses, "
                f"last read {seen!r}; with the harness' window focused the "
                "wl-clipboard write-back has nowhere to land")
        attempt += 1
    return False, (
        f"the pair gesture produced no fresh 'translating' journal line in 90 s "
        f"({attempt} presses) - the session never registered the shortcut, or "
        "the layout the test asked for is not the one the keys produce")


# ---------------------------------------------------------------------------
# Visibility, not vibes: the suite is slow or it is not, and "slow" is not a
# finding. Every guest command is timed in Grid.run; these two hooks make the
# numbers appear in the run output itself, so nobody fixes a timeout to make a
# red test green without seeing what the red test was waiting for.
# ---------------------------------------------------------------------------

def pytest_terminal_summary(terminalreporter, config=None):
    from grid import SLOWLOG
    if not SLOWLOG:
        return
    total = sum(t for t, _, _ in SLOWLOG)
    terminalreporter.write_sep("=", "guest commands")
    terminalreporter.write_line(
        f"{len(SLOWLOG)} commands, {total:.1f}s of ssh; "
        f"{sum(1 for t, _, _ in SLOWLOG if t >= 0.5)} of them over 0.5s")
    for took, vmname, cmd in sorted(SLOWLOG, reverse=True)[:12]:
        terminalreporter.write_line(f"{took:6.2f}s  {vmname}: {cmd[:110]}")


def pytest_sessionfinish(session, exitstatus):
    from grid import close_all
    close_all()
