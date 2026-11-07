"""E2E-8: the finished translation is written back to the clipboard.

CopyResult=true (the default behaviour users rely on: Ctrl+V pastes the
translation). After the gesture the clipboard must go from the source
text to the streamed result: `[MOCK -> en] <source>` — a value the DE
apps (Klipper, any Wayland paste target) receive.

Red check: with CopyResult=false the clipboard must keep the source.
"""
import time

SEED = "Paste me back 77"


def test_translation_written_back_to_clipboard(env):
    vm = env
    vm.clipset(SEED)
    ok, _ = vm.focus_fresh_term()
    assert ok, ("no foreign window took the focus; the helper said: " + str(_))
    # KGA pair: the XI2 Ctrl+C+C listener is inert on KDE/Wayland
    # (XWayland never emits XI2 raw events; xinput --root = 0 RAWKEY).
    vm.double_ctrl_alt_c()
    got = ""
    for _ in range(100):        # 0.2 s ticks; the paste is there long before 20 s
        time.sleep(0.2)
        _, got = vm.ses("wl-paste")
        if got.strip() != SEED and "MOCK" in got:
            break
    assert got.strip().startswith("[MOCK -> ") and SEED in got, \
        f"clipboard was not replaced with the result: {got!r}"
