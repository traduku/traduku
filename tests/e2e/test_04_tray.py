"""E2E-4: the tray icon (StatusNotifierItem) is present in the DE panel.

The app registers an SNI item; the desktop's watcher lists it and the item
itself says who it is. D-Bus is the machine-checkable signal, the screenshot
is the visual record.

Desktop-aware on purpose (ROADMAP.md §4.1: this test used to know only
Plasma). The watcher name is the same on both - Plasma's own tray host and
GNOME's appindicator extension both own org.kde.StatusNotifierWatcher - but
only one of them is guaranteed to exist: a GNOME without the extension has
no host at all, and the app runs tray-less there by design (the .deb only
`Suggests:` the extension, ROADMAP.md §2). Without a host the test SKIPs
with the reason conftest.ensure_sni_host() measured; it never passes by
asserting nothing.

Identity is asserted on the Title/Id the panel would show. Measured before
the fix, the two shells disagreed - "Traduku" on Plasma, "traduku" on GNOME,
because Qt derives both properties from the application display name and only
the KDE shell set it (see tests/unit/tst_cli.cpp::panelNameIsTheSameOnBothShells).

Red checks (verified during development): killing the app removes the item;
an app that never registered fails the identity search; clearing the display
name (git revert of the configureApplication line) fails it on GNOME.
"""
import re
import time

import pytest

from conftest import ensure_sni_host
# (pytest is imported because the SNI-host check can skip: on debian-gnome no
# appindicator host is present at all, and this suite skipped by crashing here
# with NameError until the import was added - a skip path nobody had ever taken
# is as untested as a red one.)

WATCHER = "org.kde.StatusNotifierWatcher"


def _items(vm):
    """The watcher's list, parsed.

    Both hosts spell the entries differently (measured): Plasma registers
    ':1.70/StatusNotifierItem', the appindicator extension
    ':1.79@/StatusNotifierItem'. Parsing both is what lets one assertion
    cover both desktops.
    """
    rc, out = vm.ses(
        f"gdbus call --session -d {WATCHER} -o /StatusNotifierWatcher"
        " -m org.freedesktop.DBus.Properties.Get"
        f" {WATCHER} RegisteredStatusNotifierItems")
    entries = []
    for raw in re.findall(r"'([^']+)'", out):
        m = re.match(r"^(:[\d.]+)(?:@|)(/.*)$", raw)
        if m:
            entries.append((m.group(1), m.group(2)))
    return out, entries


def _identities(vm):
    """[(service, path, title, id, tooltip)] as the items themselves report them.

    ToolTip is in the list because it is the property TRADUKU writes
    (QtTray::refreshToolTip -> "Traduku - <hint>"), while Title and Id are
    derived by Qt from its own application name - and which name it uses is a
    Qt-version detail, not something this project owns. Measured with the same
    source on the two guests: Plasma reports Title/Id = "Traduku", Ubuntu's
    GNOME guest (Qt 6.8) reports "traduku" for both while its ToolTip is
    "Traduku - Press Ctrl+C twice to translate the clipboard". The identity a
    user reads in the panel is the tooltip, so that is what the assertion
    pins; Title/Id are still required to name us, case-insensitively.
    """
    items, entries = _items(vm)
    out = []
    for svc, path in entries:
        props = {}
        for prop in ("Title", "Id", "ToolTip"):
            rc, v = vm.ses(
                f"gdbus call --session -d {svc} -o {path}"
                " -m org.freedesktop.DBus.Properties.Get"
                f" org.kde.StatusNotifierItem {prop}")
            # ToolTip is a (icon, struct, text, hints) tuple: the text is the
            # third string, not the first - take the last non-empty one.
            found = re.findall(r"'<([^']*)'?>", v) or re.findall(r"'([^']*)'", v)
            props[prop.lower()] = max(found, key=len) if found else ""
        out.append((svc, path, props["title"], props["id"], props["tooltip"]))
    return items, out


def _ours(identities):
    """The item that is ours, by the name the panel shows the user."""
    return [i for i in identities
            if "traduku" in (i[2] + " " + i[3]).lower()
            and i[4].startswith("Traduku")]


def test_tray_item_registered_in_the_panel(env):
    vm = env
    reason = ensure_sni_host(vm)
    if reason:
        # Not a silent pass: the reason is the measurement, and it names the
        # fix (install the extension, or recreate the image so it is enabled).
        pytest.skip(reason)

    items, identities = _identities(vm)
    assert identities, f"watcher lists no item at all: {items}"
    ours = _ours(identities)
    assert ours, (f"no registered item says it is Traduku (seen: "
                  f"{[(i[2], i[3], i[4]) for i in identities]})")
    # The panel must show the product's name, not the executable's, and the
    # hint that tells the user what to press - both are set by QtTray.
    assert ours[0][4].startswith("Traduku - "), \
        f"tooltip does not name the product: {ours[0][4]!r}"
    assert "Ctrl+C" in ours[0][4], f"tooltip lost the gesture hint: {ours[0][4]!r}"
    vm.shot("e2-tray-panel")  # visual evidence of the panel icon

    # red-check mechanism: killing the app must remove the item
    vm.ses("pkill -x traduku")
    # Poll the watcher until it forgets us: three seconds of clock was the
    # guess, "the bus no longer lists the item" is the claim. Bounded, and the
    # message says which of the two failed - a dead app or a stale registry.
    deadline = time.time() + 15
    _, after = _identities(vm)
    while _ours(after) and time.time() < deadline:
        time.sleep(0.2)
        _, after = _identities(vm)
    assert not _ours(after), ("the item is still registered 15 s after the app was "
                              f"killed (SNI Close() ignored?): {after}")
