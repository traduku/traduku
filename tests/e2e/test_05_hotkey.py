"""E2E-5: the double-press translation hotkey fires (Wayland session).

On native Wayland global key observation is forbidden — the app knows this
and registers a global shortcut via KGlobalAccel (default Ctrl+Alt+C);
the pair detection reuses the same DoublePressDetector as the passive
Ctrl+C+C gesture. QMP injects SCANCODES (physical keys), so the test
verifies layout-independence the way the code promises: with the ru
layout active, the same physical key presses must fire.

The single-press passivity check is done on Ctrl+Alt+C: one press must do
nothing (a plain global shortcut would fire immediately).

(Passive Ctrl+C+C on X11/XWayland-focus is the same detector; exercised
in the debian-kde X11 job — see PIPELINE.md matrix.)
"""
import time

from conftest import restart_kwin

SEED = "Translate me 12345"
RU_SEED = "ru layout text 67890"


def _gesture(vm, seq="ctrl alt c;250;ctrl alt c"):
    vm.keyseq(seq)


def _clip(vm):
    return vm.ses("wl-paste 2>/dev/null")[1]


def _wait_mock(vm, seconds=25):
    """Authoritative signal: a translation was logged inside a sliding
    20s window (stale entries from earlier phases age out); the clipboard
    check is the user-visible half of the effect."""
    for _ in range(seconds):
        time.sleep(1)
        rc, out = vm.journal("--since -20s", grep="translating")
        if out.strip():
            return "MOCK" in _clip(vm)
    return False


def test_double_press_hotkey_layouts(env):
    vm = env
    # two layouts, switchable Alt+Shift (KWin kxkb), start on us
    restart_kwin(vm, "us,ru")

    # single press must NOT translate (pair detection, not a plain shortcut)
    vm.clipset(SEED)
    vm.keyseq("ctrl alt c")
    time.sleep(1.5)
    assert _clip(vm).strip() == SEED, "single press fired (must be a PAIR)"

    # double press fires
    vm.clipset(SEED)
    _gesture(vm)
    assert _wait_mock(vm), f"us gesture did not translate (clip: {_clip(vm)!r})"

    # ru layout: identical scancodes must fire (physical keycode matching)
    vm.keyseq("alt shift")
    time.sleep(1)
    vm.clipset(RU_SEED)
    _gesture(vm)
    assert _wait_mock(vm), f"ru gesture did not translate (clip: {_clip(vm)!r})"
    vm.keyseq("alt shift")  # back to us for the next tests
