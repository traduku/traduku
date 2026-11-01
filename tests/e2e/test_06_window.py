"""E2E-6: the translation window appears on the gesture.

QMP screendump = the compositor's framebuffer (works on native Wayland).
A mapped Traduku window changes a visible area of the screen; the test
asserts the changed-area fraction and keeps pre/post shots as evidence.

Red check: with no gesture, two shots of an idle session must NOT differ
by the threshold.
"""
import time

SEED = "Window appears 42"


def test_window_appears_after_gesture(env):
    vm = env
    vm.ses("pkill -x xterm; wmctrl -c Traduku 2>/dev/null; true")
    time.sleep(1)
    vm.shot("e2-win-pre")
    # idle: no spontaneous change
    vm.shot("e2-win-idle")
    idle = vm.diff_frac("e2-win-pre", "e2-win-idle")
    assert idle < 0.02, f"idle screen changed by {idle:.2f} (noise breaks the test)"

    vm.ses("setsid xterm -e sleep 300 >/dev/null 2>&1 & sleep 1;"
           "xdotool windowactivate $(xdotool search -name xterm | head -1) 2>/dev/null; true")
    vm.clipset(SEED)
    vm.keyseq("ctrl c;250;ctrl c")
    frac = 0.0
    for _ in range(15):
        time.sleep(1)
        vm.shot("e2-win-post")
        frac = vm.diff_frac("e2-win-pre", "e2-win-post")
        if frac > 0.05:
            break
    assert frac > 0.05, f"no visible window after gesture (changed area {frac:.3f})"
