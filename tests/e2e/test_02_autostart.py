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

    cur = vm.cursor()
    vm.ses("setsid nohup traduku >/tmp/traduku.log 2>&1 &")
    # The app's own startup line, not four seconds of hoping. Read from the
    # JOURNAL, not from the log this launch redirects its output to: measured on
    # arch-kde with fd 1 and fd 2 both pointing at /tmp/traduku.log, every
    # qInfo() the KF6-linked shell emits still arrives in the user journal
    # (journalctl shows "traduku: ready (kde, ...)" while the file holds only
    # ALSA's own writes). Waiting on the file therefore timed out on a healthy
    # app - the log is kept as the second witness in the failure message, since
    # it is where a non-Qt writer (ALSA, a crashing subprocess) does speak.
    # Either witness, both written by the app itself: see Grid.wait_ready. This
    # launch is `setsid nohup`, so on the Debian X11 session there is no
    # JOURNAL_STREAM for Qt to talk to journald with, and a journal-only wait
    # called a healthy app dead - measured: the log held "traduku: ready (kde,
    # x11; clipboard=x11 hotkey=x11-xi2 tray=yes)" while the journal had
    # nothing but kded6.
    rc, _ = vm.wait_ready(cur, path="/tmp/traduku.log", timeout=25)
    assert rc == 0, ("app never said it was up, in the journal or in its own log. "
                     "journal said:\n" + vm.ses(
                         "journalctl --user --no-pager --after-cursor='%s' | tail -15" % cur)[1]
                     + "\n/tmp/traduku.log said:\n"
                     + vm.run("head -20 /tmp/traduku.log")[1])

    # the welcome/autostart question is a modal dialog: something appeared
    vm.shot("e2-autostart-offer")
    vm.keyseq("ret")            # default button = Yes -> enable autostart
    # The answer's consequence is a file; wait for the file the test is about
    # to read (2 s of clock used to stand in for that causality).
    rc, _ = vm.wait_until(f"test -f {USER_AUTOSTART}", timeout=15, tick=0.2)
    assert rc == 0, f"answering Yes wrote no {USER_AUTOSTART}"
    vm.keyseq("esc")            # close the first-run Settings dialog

    rc, out = vm.run(f"cat {USER_AUTOSTART}")
    assert rc == 0, "no XDG autostart entry after answering Yes"
    assert "Exec=/usr/bin/traduku" in out, f"bad Exec: {out}"
    assert "X-KDE-autostart-after=panel" in out, "no panel-ordering key"
    assert "X-GNOME-Autostart-enabled" in out or "Hidden=true" not in out
    vm.ses("pkill -x traduku; true")
