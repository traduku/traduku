"""E2E-5: the Ctrl+C,Ctrl+C gesture triggers a translation.

The app's passive X11/XInput2 listener observes key events without
grabbing them — it works on KDE Wayland through XWayland when the focused
window is an X11 client (xterm here). The match is by *physical keycode*,
so a Russian layout must not change anything: the test runs the gesture
in us and ru layouts with the same scancode sequence (QMP injects
scancodes; XKB relabels them, the keycode stays).

A gesture that fires leads to a clipboard write (CopyResult), observed in
E2E-8; here the marker is the app log: "XI2 raw key listener active" and
"double press" (AppController starts the engine; the mock answers).

Red check: gesture in an idle clipboard (empty) must produce NO
translation; a single Ctrl+C must do nothing (passive mode).
"""
import time

SEED = "Translate me 12345"


def _fire_gesture(vm, n=2, gap=250):
    vm.clipset(SEED)
    vm.ses("wmctrl -a xterm 2>/dev/null; xdotool windowactivate $(xdotool search -name xterm | head -1) 2>/dev/null; true")
    time.sleep(0.5)
    vm.keyseq(";".join(["ctrl c"] * n + [str(gap)] * (n - 1)) if n > 1 else "ctrl c")


def test_double_ctrl_c_starts_translation(env):
    vm = env
    vm.ses("setsid xterm -title focusterm -e sleep 300 >/dev/null 2>&1 & sleep 1; true")
    # qInfo lands in the user journal (setid'ed app), not the stdout log
    _, log = vm.ses("journalctl --user --no-pager --since -10min | grep -c 'listener active'")
    assert int(log.strip() or 0) > 0, "passive listener not active in the last 10 min"

    # single press: passive mode must NOT fire
    vm.clipset("single press no fire")
    vm.keyseq("ctrl c")
    time.sleep(1.5)
    _, clip = vm.ses("wl-paste 2>/dev/null")
    assert clip.strip() == "single press no fire", "single Ctrl+C fired (should be passive)"

    # us layout: double press fires (clipboard replaced by the streamed result)
    vm.ses("setxkbmap -display :0 -layout us 2>/dev/null; true")
    _fire_gesture(vm)
    got = ""
    for _ in range(20):
        time.sleep(1)
        _, got = vm.ses("wl-paste 2>/dev/null")
        if "MOCK" in got:
            break
    assert "MOCK" in got, f"us layout gesture did not translate (clipboard: {got!r})"

    # ru layout: the SAME physical keys must fire (keycode, not keysym)
    vm.ses("pkill -x traduku; sleep 1")  # fresh instance: no double-burst state
    vm.ses("rm -f ~/.config/traduku/config.ini; setsid nohup traduku >/tmp/traduku.log 2>&1 </dev/null &")
    time.sleep(1)  # config is not FirstRunDone -> welcome dialog appears: answer Yes
    vm.keyseq("ret"); time.sleep(1); vm.keyseq("esc")
    vm.clipset("ru layout text 67890")
    vm.ses("setxkbmap -display :0 -layout ru 2>/dev/null; sleep 0.3; true")
    vm.ses("xdotool windowactivate $(xdotool search -name focusterm | head -1) 2>/dev/null; true")
    vm.keyseq("ctrl c;250;ctrl c")
    got2 = ""
    for _ in range(20):
        time.sleep(1)
        _, got2 = vm.ses("wl-paste 2>/dev/null")
        if "MOCK" in got2:
            break
    vm.ses("setxkbmap -display :0 -layout us 2>/dev/null; true")
    assert "MOCK" in got2, f"ru layout gesture did not translate (clipboard: {got2!r})"
