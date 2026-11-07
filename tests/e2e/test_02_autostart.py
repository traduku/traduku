"""E2E-2: autostart installation — the two desktops install it differently.

A program whose only interface is a key gesture is dead code until it runs,
so "it comes back at login" is a product promise, not a nicety. There are
two ways to earn it and they are deliberately different (ROADMAP.md §2: the
shells do not share a dependency set or a session model):

* Plasma: the app ASKS. Nothing may write an autostart entry for the user
  without a yes, so the first launch shows the "Welcome to Traduku" dialog,
  Enter = Yes writes ~/.config/autostart/traduku.desktop, and this test
  drives exactly that (including the Escape that closes the settings wizard).
  Red check (verified during development): with the dialog NOT answered
  (killed before Enter) the autostart file must NOT appear.

* GNOME: the PACKAGE ships it. There is no first-run dialog in this shell
  yet (Stage 6), and "install, reboot, press Ctrl+C+C, nothing happens" is
  the exact silence this project refuses — so traduku-gnome installs
  /etc/xdg/autostart/traduku.desktop and the entry is visible/editable in
  GNOME's own Startup Applications UI. Test: the file is in the package, its
  Exec points at the binary, nothing hides it, and it carries no KDE-only
  keys (the whole reason packaging/gnome/traduku.desktop exists).
  Red check: `dpkg -P traduku-gnome` (or pacman -Rns) makes it vanish.
"""
import time

import pytest

PACKAGED_AUTOSTART = "/etc/xdg/autostart/traduku.desktop"
USER_AUTOSTART = "/home/vm/.config/autostart/traduku.desktop"


def _owner(vm, path):
    """Which installed package owns `path` — 'the package ships it' has to be
    the package manager's answer, not the file's existence."""
    if "arch" in vm.vm:
        rc, out = vm.run(f"pacman -Qo {path} 2>&1")
    else:
        rc, out = vm.run(f"dpkg -S {path} 2>&1")
    return rc, out


def test_packaged_autostart_entry_ships_with_the_gnome_package(session):
    """The GNOME flavour's autostart: installed by the package, not by a dialog."""
    vm = session
    if not vm.vm.endswith("-gnome"):
        pytest.skip("the Plasma flavour asks on first run instead "
                    "(test_first_run_asks_and_writes_xdg_autostart)")

    rc, content = vm.run(f"cat {PACKAGED_AUTOSTART}")
    assert rc == 0, (f"no {PACKAGED_AUTOSTART}: the GNOME shell has no first-run "
                     "dialog yet (Stage 6), so without a packaged autostart entry "
                     "a fresh install does nothing until someone starts it by hand")
    rc, owner = _owner(vm, PACKAGED_AUTOSTART)
    assert rc == 0 and "traduku" in owner, f"not owned by the package: {owner}"
    execs = [l.strip() for l in content.splitlines() if l.startswith("Exec=")]
    assert execs and any(l.split("=", 1)[1].strip().endswith("traduku") for l in execs), \
        f"no Exec pointing at the binary: {content}"
    assert "Hidden=true" not in content, f"the entry is Hidden, gnome-session will skip it: {content}"
    assert "X-KDE" not in content, ("the GNOME autostart entry carries KDE-only keys - "
                                f"packaging/gnome/traduku.desktop must not: {content}")
    assert "Type=Application" in content and "Name=Traduku" in content, f"not a session entry: {content}"


def test_first_run_asks_and_writes_xdg_autostart(session):
    """The Plasma flavour's autostart: asked for, answered, written."""
    vm = session
    if vm.vm.endswith("-gnome"):
        pytest.skip("the GNOME flavour ships the entry in the package "
                    "(test_packaged_autostart_entry_ships_with_the_gnome_package); "
                    "its first-run dialog is Stage 6")
    vm.ses("pkill -x traduku; true")
    vm.run(f"rm -f {USER_AUTOSTART} ~/.config/traduku/config.ini")

    vm.ses("setsid nohup traduku >/tmp/traduku.log 2>&1 &")
    time.sleep(4)
    assert vm.run("pgrep -x traduku")[0] == 0, "app did not start"

    # the welcome/autostart question is a modal dialog: something appeared
    vm.shot("e2-autostart-offer")
    vm.keyseq("ret")            # default button = Yes -> enable autostart
    time.sleep(2)
    vm.keyseq("esc")            # close the first-run Settings dialog
    time.sleep(1)

    rc, out = vm.run(f"cat {USER_AUTOSTART}")
    assert rc == 0, "no XDG autostart entry after answering Yes"
    assert "Exec=/usr/bin/traduku" in out, f"bad Exec: {out}"
    assert "X-KDE-autostart-after=panel" in out, "no panel-ordering key"
    assert "X-GNOME-Autostart-enabled" in out or "Hidden=true" not in out
    vm.ses("pkill -x traduku; true")
