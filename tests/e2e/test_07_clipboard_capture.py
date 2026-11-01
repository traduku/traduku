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
    vm.keyseq("ctrl c;250;ctrl c")
    got = ""
    for _ in range(20):
        time.sleep(1)
        _, got = vm.ses("wl-paste")
        if "MOCK" in got:
            break
    assert SEED in got, f"DE clipboard source not captured (result: {got!r})"
