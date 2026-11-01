"""E2E-3: the app starts by itself after an OS restart (autostart works).

Prerequisite: E2E-2 left the XDG autostart entry in place. Reboot the whole
VM (systemd reboot over SSH), wait for the graphical session, and the app
must be running WITHOUT anyone starting it.

Red check (verified during development): with the autostart entry removed
before the reboot, the process must NOT be there afterwards.
"""
import time


def test_app_autostarts_after_reboot(session):
    vm = session
    rc, _ = vm.run("test -f ~/.config/autostart/traduku.desktop")
    assert rc == 0, "autostart entry missing (run E2E-2 first)"
    ses_cmd = "true"
    vm.sudo("systemctl reboot")
    assert vm.wait_ssh(300), "VM did not come back after reboot"
    # session + autostart need a moment
    for _ in range(30):
        if vm.run("pgrep -x kwin_wayland || pgrep -x kwin_x11")[0] == 0:
            break
        time.sleep(3)
    time.sleep(15)
    rc, out = vm.run("pgrep -x traduku && echo RUNNING")
    assert rc == 0 and "RUNNING" in out, "traduku did not autostart after reboot"
    # prove it is a real session process, not a stray shell
    rc, out = vm.run("tr '\\0' ' ' < /proc/$(pgrep -x traduku | head -1)/environ | grep -o 'XDG_SESSION_TYPE=\\w*'")
    assert "XDG_SESSION_TYPE=wayland" in out, f"autostarted outside a wayland session: {out}"
