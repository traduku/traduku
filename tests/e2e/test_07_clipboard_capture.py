"""E2E-7: the app reads the DE-standard clipboard.

On KDE/Plasma Wayland the standard clipboard is KWin's + Klipper
(org.kde.KClipboard). The test seeds it with wl-copy (the Wayland-native
tool, what DE apps use) and the gesture must translate *that* text — the
mock echoes the source, so a result containing the seeded text proves the
app captured the DE clipboard (not X11 CLIPBOARD of a private display,
not a stale selection).

Red check: an unrelated X11-only clipboard write (xclip on :0) is NOT
what a Wayland app sees — seeding via wl-copy is the DE-standard check.
"""
import time

SEED = "Klipper standard clipboard 99"


def test_app_captures_de_clipboard(env):
    vm = env
    vm.clipset(SEED)
    _, clip = vm.ses("wl-paste")
    assert SEED in clip, f"wl-copy seed missing from DE clipboard: {clip!r}"

    vm.ses("setsid xterm -e sleep 300 >/dev/null 2>&1 & sleep 1;"
           "xdotool windowactivate $(xdotool search -name xterm | head -1) 2>/dev/null; true")
    # Gesture: the DE global-shortcut pair (KGlobalAccel). The XI2 passive
    # Ctrl+C+C path is structurally dead on KDE/Wayland: KWin injects keys
    # into XWayland per focused window, XI2 RAW events are never generated
    # (verified: xinput --root sees 0 RAWKEY even for xdotool/XTEST keys).
    # XI2 coverage belongs to the X11 session job (debian-kde X11).
    vm.double_ctrl_alt_c()
    got = ""
    for _ in range(20):
        time.sleep(1)
        _, got = vm.ses("wl-paste")
        if "MOCK" in got:
            break
    # Must be the RESULT (mock echo), not the untouched seed: asserting only
    # SEED passes vacuously when the gesture never fires (observed).
    assert "MOCK" in got and SEED in got, \
        f"DE clipboard source not captured (result: {got!r})"
