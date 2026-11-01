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


@pytest.fixture(scope="session")
def session(vm):
    """Prepare the graphical session: env capture, no idle-lock, mock LLM, config."""
    rc, out = vm.run("pgrep -u vm -x kwin_wayland || pgrep -u vm -x kwin_x11")
    assert rc == 0, "no KDE session in the guest (autologin broken?)"
    vm.run(
        "echo vm | sudo -S cat /proc/$(pgrep -u vm -x kwin_wayland | head -1)"
        " | true; P=$(pgrep -u vm -x kwin_wayland | head -1);"
        " [ -z \"$P\" ] && P=$(pgrep -u vm -x kwin_x11 | head -1);"
        " echo vm | sudo -S cat /proc/$P/environ | tr '\\0' '\\n'"
        " | grep -E '^(DISPLAY|WAYLAND_DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_RUNTIME_DIR|XDG_SESSION_TYPE)='"
        " | sed 's/^/export /' > /tmp/session.env;"
        " grep -q WAYLAND_DISPLAY /tmp/session.env || echo 'export WAYLAND_DISPLAY=wayland-0' >> /tmp/session.env;"
        " grep -q XDG_RUNTIME_DIR /tmp/session.env || echo 'export XDG_RUNTIME_DIR=/run/user/1000' >> /tmp/session.env"
    )
    vm.sudo("loginctl unlock-sessions")
    vm.run("rm -f ~/.config/traduku/config.ini; pkill -x traduku; true")
    return vm


def ses(vm, script, timeout=120):
    """Run a snippet inside the graphical session."""
    return vm.run(f"source /tmp/session.env; {script}", timeout=timeout)
