"""Session fixtures for the traduku GUI grid tests.

TRADUKU_VM selects the grid VM (default arch-kde — the program's native
environment, KDE Plasma Wayland, see PIPELINE.md).
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
        "P=$(pgrep -u vm -x kwin_wayland | head -1); [ -z \"$P\" ] && P=$(pgrep -u vm -x kwin_x11 | head -1);"
        " echo vm | sudo -S cat /proc/$P/environ | tr '\\0' '\\n'"
        " | grep -E '^(DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_RUNTIME_DIR|XDG_SESSION_TYPE)='"
        " | sed 's/^/export /' > /tmp/session.env;"
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


@pytest.fixture(scope="session")
def session(vm):
    """Prepare the graphical session: env capture, no idle-lock, mock LLM, config."""
    rc, out = vm.run("pgrep -u vm -x kwin_wayland || pgrep -u vm -x kwin_x11")
    assert rc == 0, "no KDE session in the guest (autologin broken?)"
    capture_sessionenv(vm)
    vm.sudo("loginctl unlock-sessions")
    vm.run("rm -f ~/.config/traduku/config.ini; pkill -x traduku; true")
    return vm


def ses(vm, script, timeout=120):
    """Run a snippet inside the graphical session."""
    return vm.run(f"source /tmp/session.env; {script}", timeout=timeout)


@pytest.fixture(scope="session")
def env(session):
    """Live GUI environment: mock LLM endpoint, config, app running in-session.

    The app is the *installed* binary; the mock streams `[MOCK -> lang] <text>`.
    """
    vm = session
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

    vm.run("mkdir -p ~/.config/traduku; cat > ~/.config/traduku/config.ini <<'INI'\n"
           "[General]\nApiUrl=http://127.0.0.1:8931/v1\nModel=mock\n"
           "TargetLanguage=en\nPressInterval=0.8\nSoundsEnabled=false\n"
           "CopyResult=true\nFirstRunDone=true\nINI\n"
           "grep -c ApiUrl ~/.config/traduku/config.ini")

    vm.ses("pkill -x traduku; sleep 0.5; setsid nohup traduku >/tmp/traduku.log 2>&1 </dev/null &")
    import time
    time.sleep(4)
    assert vm.run("pgrep -x traduku")[0] == 0, "app not running in session"
    return vm


@pytest.fixture(autouse=True)
def app_running(env):
    """Every GUI test starts from a fresh app instance.

    A restart (not a liveness check) isolates the double-press detector:
    key events from a previous/interrupted test must never pair with this
    test's presses (observed: an interrupted run's keyseq polluted the
    next single-press assertion).
    """
    capture_sessionenv(env)  # reboot/kwin restart rotates WAYLAND_DISPLAY/xauth
    env.ses("pkill -x traduku; sleep 1; "
            "setsid nohup traduku >/tmp/traduku.log 2>&1 </dev/null &")
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
        # KWin restarts leave stale KGlobalAccel registrations (a single
        # press then arrives N times -> the pair detector fires instantly);
        # bounce the KGA daemon so the app's re-registration is clean.
        "setsid sh -c 'systemctl --user restart plasma-kglobalaccel.service plasma-kwin_wayland.service' >/dev/null 2>&1 &"]))
    import time
    for _ in range(20):
        time.sleep(2)
        rc, _ = vm.ses("timeout 5 setxkbmap -display :0 -query")
        if rc == 0:
            break
    time.sleep(1)
    capture_sessionenv(vm)
    # KWin restart kills the KGlobalAccel registrations of every app;
    # bring the translator back so its shortcut exists in the new KWin.
    vm.ses("pkill -x traduku; sleep 1; "
           "setsid nohup traduku >/tmp/traduku.log 2>&1 </dev/null &")
    for _ in range(10):
        time.sleep(1)
        rc, _ = vm.run("pgrep -x traduku")
        if rc == 0:
            break
