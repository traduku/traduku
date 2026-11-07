"""Session fixtures for the traduku GUI grid tests.

TRADUKU_VM selects the grid VM (default arch-kde — the program's native
environment, KDE Plasma Wayland, see ROADMAP.md §6).
"""
import os
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
    if "arch" in vm.vm:
        vm.ses("setsid sh -c 'systemctl --user restart plasma-kglobalaccel.service plasma-kwin_wayland.service' >/dev/null 2>&1 &")
    else:
        vm.ses("setsid sh -c 'systemctl --user restart plasma-kglobalaccel.service' >/dev/null 2>&1 &")
    import time
    if "arch" in vm.vm:
        for _ in range(20):
            time.sleep(2)
            rc, _ = vm.ses("ls /run/user/1000/ | grep -E '^wayland-[0-9]+$' >/dev/null && pgrep -u vm -x kwin_wayland")
            if rc == 0:
                break
    # env/app starts must not race the bounced KGA (see restart_kwin note)
    for _ in range(20):
        rc, _ = vm.ses("gdbus call -e -d org.kde.kglobalaccel -o /kglobalaccel"
                       " -m org.freedesktop.DBus.Introspectable.Introspect")
        if rc == 0:
            break
        time.sleep(1)
    time.sleep(2)
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
    from grid import ROOT
    vm.push(ROOT / "tools" / "mock-openai.py", "~/mock-openai.py")
    # pkill/pgrep -f also match OUR OWN ssh shell argv (it contains the
    # script path); exclude our pid — only the real server dies.
    vm.run("for p in $(pgrep -f mock-openai.py 2>/dev/null; true); do "
           "[ \"$p\" != \"$$\" ] && kill \"$p\" 2>/dev/null; done; sleep 0.5; "
           "setsid nohup python3 /home/vm/mock-openai.py 8931 >/tmp/mock.log 2>&1 </dev/null &")
    import time
    time.sleep(1)
    rc, out = vm.run("curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8931/v1/models")
    assert rc == 0 and "200" in out, "mock endpoint is not serving"


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
    vm.start_app()
    import time
    time.sleep(4)
    assert vm.run("pgrep -x traduku")[0] == 0, "app not running in session"
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
    env.ses("pkill -x traduku; sleep 1;")
    write_mock_config(env)
    if env.run("command -v traduku")[0] != 0:
        return env           # see the env-fixture note (uninstalled platform)
    env.start_app()
    import time
    time.sleep(4)
    assert env.run("pgrep -x traduku")[0] == 0, "app did not (re)start"
    return env


def restart_kwin(vm, layouts):
    """Set the KDE keyboard layouts and restart KWin (kxkb reads the list
    at startup). The Wayland socket gets a new number -> re-capture."""
    # kxkbrc is the file the kxkb KCM (and KWin) use; restarting KWin is
    # mandatory: the layout list is read at compositor startup. The restart
    # runs detached (ssh would block on the D-Bus job) and X readiness is
    # polled, not slept on (XWayland comes back a moment after KWin).
    vm.ses(" ".join([
        f"kwriteconfig6 --file kxkbrc --group Layout --key LayoutList '{layouts}'",
        "kwriteconfig6 --file kxkbrc --group Layout --key SwitchMode Keyboard",
        "kwriteconfig6 --file kxkbrc --group Layout --key Options grp:alt_shift_toggle",
        "kwriteconfig6 --file kxkbrc --group Layout --key Initial 0",
        # KWin restarts leave stale KGlobalAccel mappings in the compositor
        # (a single press then arrives N times -> the pair detector fires
        # instantly). Bounce ONLY KWin: it refetches the (alive, per-app,
        # single-entry) registry from KGA at startup. Restarting KGA too
        # creates a registration race: the app (re)registers while the
        # daemon is starting, and KWin - which polled the empty registry
        # on boot - never learns about it (observed in-suite: gesture
        # dead for the whole test, alive minutes later).)
        # Plasma 6.3 (debian-kde): the user unit does not own the session
        # (sddm/startplasma-wayland does) - restarting it deadlocks the
        # whole session (see session-fixture note). Layouts stay written
        # to kxkbrc; on 6.3 they apply only at next login, and test_05's
        # ru-layout leg fails there BY DESIGN until the .deb phase.
        "setsid sh -c 'systemctl --user restart plasma-kwin_wayland.service' >/dev/null 2>&1 &"
        if "arch" in vm.vm else "true"]))
    import time
    for _ in range(20):
        time.sleep(2)
        rc, _ = vm.ses("timeout 5 setxkbmap -display :0 -query")
        if rc == 0:
            break
    # KGA must be fully up BEFORE the app (re)registers: an app that
    # registers while the bounced daemon is still starting has its
    # registration silently dropped (observed in-suite: the pair gesture
    # was dead for the whole test, alive minutes later).
    for _ in range(20):
        rc, _ = vm.ses("gdbus call -e -d org.kde.kglobalaccel -o /kglobalaccel"
                       " -m org.freedesktop.DBus.Introspectable.Introspect")
        if rc == 0:
            break
        time.sleep(1)
    time.sleep(1)
    capture_sessionenv(vm)
    if vm.run("command -v traduku")[0] != 0:
        return               # nothing to register: the test body fails on its
                             # own asserts with the real (no-app) reason
    # KWin restart kills the KGlobalAccel registrations of every app;
    # bring the translator back so its shortcut exists in the new KWin.
    vm.start_app()
    for _ in range(10):
        time.sleep(1)
        rc, _ = vm.run("pgrep -x traduku")
        if rc == 0:
            break
    # End-to-end readiness gate. On Plasma 6 kglobalaccel lives INSIDE
    # kwin_wayland: a kwin restart drops the app's registration, and the
    # KDE client library re-registers only when the bus name reappears -
    # on a young session that lag exceeds any sane sleep (observed in
    # suite: gesture dead for the whole test, alive ~30s later; kwin's
    # KGA-registry D-Bus does not expose the ephemeral component, so
    # probe the real path). Fire the pair gesture until it produces a
    # FRESH journal line; the warm-up translation is harmless (the
    # sliding-window journal checks in tests age it out; clipset reseeds).
    for attempt in range(15):
        cur = vm.cursor()
        vm.clipset(f"restart_kwin warmup {attempt}")
        vm.double_ctrl_alt_c()
        for _ in range(4):
            time.sleep(1)
            if vm.journal_after(cur, grep="translating.*8931")[1].strip():
                return
        time.sleep(3)
    raise RuntimeError("global shortcut never came back after the kwin restart")
