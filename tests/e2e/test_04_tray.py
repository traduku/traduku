"""E2E-4: the tray icon (StatusNotifierItem) is present in the DE panel.

KDE Plasma's system tray is an SNI host: the app registers a
StatusNotifierItem and the watcher lists it. Plasma reports items by
sender; identity = the Title property (KdeTray sets "Traduku"). D-Bus is
the exact machine-checkable signal; the panel screenshot is the visual
record (on GNOME without the SNI extension there is no watcher — a known
DE difference from PIPELINE.md; on KDE the icon is mandatory).

Red check (verified during development, see run log): killing the app
removes the item and the test fails; an app that never registered fails
the Title search.
"""
import re
import time

def _items(vm):
    rc, out = vm.ses(
        "gdbus call --session -d org.kde.StatusNotifierWatcher "
        "-o /StatusNotifierWatcher -m org.freedesktop.DBus.Properties.Get "
        "org.kde.StatusNotifierWatcher RegisteredStatusNotifierItems")
    return rc, out


def _titles(vm):
    _, items = _items(vm)
    titles = []
    for sender in re.findall(r":\d+\.\d+", items):
        rc, t = vm.ses(
            f"gdbus call --session -d {sender} -o /StatusNotifierItem "
            "-m org.freedesktop.DBus.Properties.Get org.kde.StatusNotifierItem Title")
        if rc == 0 and "Traduku" in t:
            titles.append((sender, t.strip()))
    return items, titles


def test_tray_item_registered_in_kde(env):
    vm = env
    items, titles = _titles(vm)
    assert "StatusNotifierItem" in items, f"watcher has no items: {items}"
    assert titles, f"no item titled Traduku among: {items}"
    vm.shot("e2-tray-panel")  # visual evidence of the panel icon

    # red-check mechanism: killing the app must remove the item
    vm.ses("pkill -x traduku")
    time.sleep(3)
    _, titles_after = _titles(vm)
    assert not titles_after, "dead app's tray item stayed registered"
