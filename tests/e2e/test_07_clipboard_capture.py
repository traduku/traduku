"""E2E-7: the app reads the DE-standard clipboard.

Seeded with the session's own clipboard tool — wl-copy where a Wayland
compositor owns the clipboard (KWin's + Klipper, org.kde.KClipboard), xclip
where the session is a native X11 Plasma (debian-kde). Either way the gesture
must translate *that* text: the mock echoes the source, so a result containing
the seeded text proves the app captured the DE clipboard, not X11 CLIPBOARD of
a private display and not a stale selection.

Red check: on a Wayland session an X11-only write (xclip on :0) is NOT what a
Wayland app sees — seeding with the session's native tool is the DE-standard
check, and reading it back through the same reader (conftest.clip) is what
keeps "waited for" and "asserted on" the same clipboard.
"""
from conftest import clip_until

SEED = "Klipper standard clipboard 99"


def test_app_captures_de_clipboard(env):
    vm = env
    vm.clipset(SEED)
    # Prove the seed got in before asking the app to read it: an assertion on
    # the clipboard the app never had would pass vacuously.
    ok, seen = clip_until(vm, SEED, timeout=6)
    assert ok, f"seed never reached the DE clipboard: {seen!r}"

    ok, why = vm.focus_fresh_term()
    assert ok, ("no foreign window took the focus; the helper said: " + str(why))
    # Gesture: the DE global-shortcut pair (KGlobalAccel). The XI2 passive
    # Ctrl+C+C path is structurally dead on KDE/Wayland: KWin injects keys
    # into XWayland per focused window, XI2 RAW events are never generated
    # (verified: xinput --root sees 0 RAWKEY even for xdotool/XTEST keys).
    # On the X11 session the passive listener is the X RECORD helper, which
    # test_05 exercises.
    vm.double_ctrl_alt_c()
    # Must be the RESULT (mock echo), not the untouched seed: asserting only
    # SEED passes vacuously when the gesture never fires (observed).
    ok, got = clip_until(vm, "MOCK", timeout=25)
    assert "MOCK" in got and SEED in got, \
        f"DE clipboard source not captured (result: {got!r})"
