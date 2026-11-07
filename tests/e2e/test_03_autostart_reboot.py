"""E2E-3: the app starts by itself after an OS restart (autostart works).

This is the user's actual first run: install, reboot, press the gesture —
nobody started anything. It does not care WHO installed the entry, only that
one exists before the reboot and that the session brings the process back:

  Plasma  ~/.config/autostart/traduku.desktop       (E2E-2 answered Yes)
  GNOME   /etc/xdg/autostart/traduku.desktop        (shipped by the package)

Which DE shell to wait for comes from the profile, not from a hardcoded
`pgrep kwin_wayland` — that line is why this test was red on a GNOME guest
for a reason that had nothing to do with autostart (ROADMAP.md §4.1).

Red check (verified during development): with every autostart entry removed
before the reboot, the process must NOT be there afterwards.
"""
import time

ENTRIES = ("/etc/xdg/autostart/traduku.desktop",           # the package's
           "/home/vm/.config/autostart/traduku.desktop")   # the dialog's


def _autostart_entries(vm):
    rc, out = vm.run("for f in %s; do [ -f \"$f\" ] && echo FOUND:$f; done; true"
                     % " ".join(ENTRIES))
    return [l.split(":", 1)[1] for l in out.splitlines() if l.startswith("FOUND:")]


def test_app_autostarts_after_reboot(session):
    vm = session
    found = _autostart_entries(vm)
    assert found, ("no XDG autostart entry anywhere before the reboot: neither the "
                   "package (GNOME) nor the first-run answer (Plasma) installed one, "
                   "so this test would prove nothing - run E2E-2 first")

    vm.sudo("systemctl reboot")
    assert vm.wait_ssh(300), "VM did not come back after reboot"
    # session + autostart need a moment; wait for the shell THIS profile promises
    shell = "gnome-shell" if vm.vm.endswith("-gnome") else "kwin_wayland || pgrep -x kwin_x11"
    for _ in range(30):
        if vm.run(f"pgrep -x {shell}")[0] == 0:
            break
        time.sleep(3)
    time.sleep(15)
    rc, out = vm.run("pgrep -x traduku && echo RUNNING")
    assert rc == 0 and "RUNNING" in out, (
        f"traduku did not autostart after reboot (entries: {found})")
    # prove it is a real session process, not a stray shell
    rc, out = vm.run("tr '\\0' ' ' < /proc/$(pgrep -x traduku | head -1)/environ | grep -o 'XDG_SESSION_TYPE=\\w*'")
    assert "XDG_SESSION_TYPE=wayland" in out, f"autostarted outside a wayland session: {out}"
