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
    vm.ses("setsid xterm -e sleep 300 >/dev/null 2>&1 & sleep 1;"
           "xdotool windowactivate $(xdotool search -name xterm | head -1) 2>/dev/null; true")
    vm.keyseq("ctrl c;250;ctrl c")
    got = ""
    for _ in range(20):
        time.sleep(1)
        _, got = vm.ses("wl-paste")
        if got.strip() != SEED and "MOCK" in got:
            break
    assert got.strip().startswith("[MOCK -> en]") and SEED in got, \
        f"clipboard was not replaced with the result: {got!r}"
