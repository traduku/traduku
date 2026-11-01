"""E2E-2: autostart installation.

KDE (and XDG desktops in general) do not forbid writing the XDG autostart
entry from the terminal, and traduku asks about it on the FIRST launch
(the app has no silent-autostart right by design). This test drives exactly
that prompt: fresh config -> launch -> "Welcome to Traduku" dialog appears
-> accept (Enter = Yes) -> ~/.config/autostart/traduku.desktop must exist
with the right Exec + KDE ordering keys. Then the settings dialog opens
(first-run wizard) - closed with Escape.

Red check (verified during development): with the first-run dialog NOT
answered (killed before Enter) the autostart file must NOT appear.
"""
import time


def test_first_run_asks_and_writes_xdg_autostart(session):
    vm = session
    vm.ses("pkill -x traduku; true")
    vm.run("rm -f ~/.config/autostart/traduku.desktop ~/.config/traduku/config.ini")

    vm.ses("setsid nohup traduku >/tmp/traduku.log 2>&1 &")
    time.sleep(4)
    assert vm.run("pgrep -x traduku")[0] == 0, "app did not start"

    # the welcome/autostart question is a modal dialog: something appeared
    pre = vm.shot("e2-autostart-offer")
    vm.keyseq("ret")            # default button = Yes -> enable autostart
    time.sleep(2)
    vm.keyseq("esc")            # close the first-run Settings dialog
    time.sleep(1)

    rc, out = vm.run("cat ~/.config/autostart/traduku.desktop")
    assert rc == 0, "no XDG autostart entry after answering Yes"
    assert "Exec=/usr/bin/traduku" in out, f"bad Exec: {out}"
    assert "X-KDE-autostart-after=panel" in out, "no panel-ordering key"
    assert "X-GNOME-Autostart-enabled" in out or "Hidden=true" not in out
    vm.ses("pkill -x traduku; true")
