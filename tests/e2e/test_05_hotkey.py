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
in the debian-kde X11 job — see ROADMAP.md §6 matrix.)
"""
import time

import pytest

from conftest import clip, clip_until, select_layout, set_layouts

SEED = "Translate me 12345"
RU_SEED = "ru layout text 67890"

# The physical key the russian leg cares about is the one the gesture's letter
# sits on; the leg itself no longer sniffs keysyms (see conftest.select_layout:
# on Wayland the injected keys are send-events, invisible to X RECORD).
def _gesture(vm, seq="ctrl alt c;250;ctrl alt c"):
    vm.keyseq(seq)


def _clip(vm):
    # The session's own reader (conftest.clip -> Grid.clipget): wl-paste on a
    # Wayland session, xclip on an X11 one. A hard-coded wl-paste here would
    # read an empty string on debian-kde, whose Plasma is native X11.
    return clip(vm)


def _wait_mock(vm, cur, seconds=25):
    """Did THIS gesture translate? The app's own log says so.

    Scoped to a journal cursor rather than a sliding 20 s window: the layout
    warm-up in set_layouts also produces "translating" lines, and with the
    suite running faster those are seconds away instead of minutes - a
    window-based match would credit this press with the warm-up's work.

    The clipboard is deliberately NOT part of this signal. On Plasma the
    translation is written back through wl-clipboard, whose X11 fallback can
    own CLIPBOARD only while an X11 window holds the focus; a gesture pressed
    with the app's own Wayland window in front therefore logs the translation
    and does not change the clipboard (measured on arch-kde: "translating 20
    chars -> English (mock)" in the journal, `wl-paste` unchanged for 10 s).
    Whether the text reaches the clipboard is a different promise, and tests
    07/08 assert it in the situation a user is in - another application focused.
    """
    rc, _ = vm.wait_journal(cur, "translating.*8931", timeout=seconds, tick=0.2)
    return rc == 0


def test_double_press_hotkey_layouts(env):
    vm = env
    # two layouts, switchable Alt+Shift (the desktop decides how), start on us
    method, why = set_layouts(vm, "us,ru")
    if method is None:
        pytest.skip((why or "this session would not carry two layouts")
                    + " - the ru leg cannot be judged here, and passing it "
                      "anyway would be a lie")

    # single press must NOT translate (pair detection, not a plain shortcut)
    vm.clipset(SEED)
    # The seed has to be demonstrably in the clipboard before pressing: the
    # assertion below is "the text is still the seed", which is meaningless -
    # and wrongly accusing - if the seed never got there.
    ok, seen = clip_until(vm, SEED)
    assert ok, f"the seed never reached the DE clipboard: {seen!r}"
    vm.keyseq("ctrl alt c")
    time.sleep(1.5)
    assert _clip(vm).strip() == SEED, "single press fired (must be a PAIR)"

    # double press fires
    vm.clipset(SEED)
    cur = vm.cursor()
    _gesture(vm)
    assert _wait_mock(vm, cur), f"us gesture did not translate (clip: {_clip(vm)!r})"
    # Let this leg finish before the next one seeds the clipboard. "translating"
    # is the start of the request, not the end of it: the write-back lands when
    # the mock's stream completes, and on a KDE session that arrived after the
    # russian leg had already put its own text on the clipboard - the app then
    # wrote over it and the test reported "the russian seed never reached the DE
    # clipboard" while the clipboard was full of the PREVIOUS leg's answer.
    ok, seen = clip_until(vm, "[MOCK -> ")
    assert ok and SEED in seen, (
        f"the us leg's answer never settled on the clipboard, so the next seed "
        f"would race it: {seen!r}")

    # ru layout: identical scancodes must fire (physical keycode matching).
    # The layout is selected and the desktop's own answer is checked - not
    # toggled by keystrokes and taken on faith. If the session is not russian,
    # this leg would pass while proving nothing, so it does not run.
    ok, why = select_layout(vm, "ru")
    if not ok:
        pytest.skip(f"the session would not be put into the ru layout: {why} "
                    "- the russian leg has nothing to test here")
    vm.clipset(RU_SEED)
    ok, seen = clip_until(vm, RU_SEED)
    assert ok, f"the russian seed never reached the DE clipboard: {seen!r}"
    cur = vm.cursor()
    _gesture(vm)
    assert _wait_mock(vm, cur), f"ru gesture did not translate (clip: {_clip(vm)!r})"
    # Hand the session back to the layout every other test assumes, and check
    # that it actually went back: a suite left in ru makes each later key test
    # quietly mean something else.
    ok, why = select_layout(vm, "us")
    assert ok, f"the session was left in the russian layout: {why}"
