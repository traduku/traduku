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

    # What the guest was running BEFORE the reboot is the promise to check
    # against afterwards - not a guess from the profile's name. Measured on
    # debian-kde: it boots Plasma on X11, so waiting 180 s for kwin_wayland and
    # then asserting XDG_SESSION_TYPE=wayland made a correct reboot fail, and
    # cost three minutes doing it.
    _, before_shell = vm.run(
        "for s in kwin_wayland kwin_x11 gnome-shell plasmashell; do "
        "pgrep -u vm -x $s >/dev/null 2>&1 && echo $s && break; done")
    before_shell = before_shell.strip()
    rc, before_type = vm.ses("echo $XDG_SESSION_TYPE")
    before_type = before_type.strip()
    assert before_shell, ("nothing was running before the reboot, so the reboot "
                          "could not be expected to bring anything back")

    vm.sudo("systemctl reboot")
    assert vm.wait_ssh(300), "VM did not come back after reboot"
    # Wait for the two things this test is about, in order, instead of for a
    # quarter of a minute: the session shell the guest was actually running,
    # then the process the autostart entry exists to start. `sleep 15` after
    # "the shell exists" stood in for both - paid in full on every fast boot,
    # and still a guess on a slow one.
    rc, _ = vm.wait_process(before_shell, timeout=180, tick=1)
    assert rc == 0, (
        f"{before_shell} - the shell that ran before the reboot - did not come "
        "back; the profile promises it before autostart can mean anything")
    rc, _ = vm.wait_process("traduku", timeout=120, tick=0.5)
    assert rc == 0, (
        f"traduku did not autostart after reboot within 120 s (entries: {found}). "
        "The journal of the session that started is the place to look.")
    # prove it is a real session process, not a stray shell. By pid, and a
    # live one: pgrep also lists an unreaped zombie, which has no /proc/environ
    # worth reading and would have made this assertion about nothing.
    pid = vm.app_pid()
    assert pid, "the traduku that autostarted vanished between the two looks"
    rc, out = vm.run(f"tr '\\0' ' ' < /proc/{pid}/environ | grep -o 'XDG_SESSION_TYPE=\\w*'")
    # The same kind of session it autostarted into before, whatever that was:
    # a Wayland guest must come back on Wayland, an X11 guest on X11. Hardcoding
    # "wayland" here failed a healthy Plasma-on-X11 reboot.
    assert f"XDG_SESSION_TYPE={before_type}" in out, (
        f"autostarted into a {out.strip() or '?'} session where the guest ran "
        f"{before_type} before the reboot")
