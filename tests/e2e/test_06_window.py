"""E2E-6: the translation window appears on the gesture.

Assertions (the task asks for "window appears"; the app has NO auto-hide
— nothing in src/ hides the window, and KWin's window list confirms it
stays mapped — so do not assert disappearing pixels):

1. The gesture fires *this* run: a "translating" journal line taken with
   a journal cursor captured BEFORE the keyseq (the 15:24 failure:
   --since -20s matched test_05's gesture, the test screenshotted 2.5 s
   in and caught the window mid-map — only the taskbar entry differed).
2. The framebuffer changes by a large fraction (QMP screendump = the
   compositor's scanout; the session fixture bakes a static color
   wallpaper, so the diff is the window, not fireworks animation).
3. The desktop itself confirms a Traduku toplevel of plausible size.
   Which desktop answers how (Grid.window_evidence): KWin's scripting
   window list on Plasma, the X server (xdotool) on an X11 session, and on
   GNOME/Wayland - where no client may enumerate another client's windows
   and gnome-shell has no scripting API - the app's own `window shown WxH`
   journal line, which is exactly why that line exists (and why it is
   asserted in tests/integration/tst_appcontroller.cpp, not only here).

Red-check A: don't fire the gesture -> assertion 1 must fail (verified:
no fresh line -> FAIL -> restore -> PASS).
Red-check B: a lock screen appearing between the idle shots -> the guard
in step 0 must fail (verified: ScreenSaver.Lock -> frac=1.000 -> trips).
The real framebuffer-noise source in this VM is the idle LOCK SCREEN
(observed ~19:15), not the wallpaper (its "fireworks" are a static
image); the session fixture disables the lock and bakes a plain color
desktop for deterministic pixels.
"""
import re
import time

SEED = "window probe 4242"


def test_window_appears_after_gesture(env):
    vm = env
    time.sleep(1)
    vm.shot("06-a")
    time.sleep(1.5)
    vm.shot("06-b")
    # Guard: the session must be quiescent before we measure. (A lock
    # screen appearing between the shots is the observed noise source.)
    idle = vm.diff_frac("06-a", "06-b")
    assert idle < 0.01, f"idle screen changes by {idle:.3f} (wallpaper/lock noise)"

    cur = vm.cursor()
    vm.clipset(SEED)
    vm.double_ctrl_alt_c()
    fired = False
    for _ in range(20):
        time.sleep(1)
        if vm.journal_after(cur, grep="translating")[1].strip():
            fired = True
            break
    assert fired, "gesture produced no FRESH 'translating' journal line"

    # The window animates in; poll the framebuffer, the change must be big
    # (980x560 on 1280x800 ≈ half the screen), not a taskbar blink.
    frac = 0.0
    for _ in range(15):
        time.sleep(1)
        vm.shot("06-open")
        frac = vm.diff_frac("06-a", "06-open")
        if frac > 0.10:
            break
    assert frac > 0.10, f"screen changed by only {frac:.3f} after the gesture"

    # Compositor-side truth, desktop-aware: the compositor/X server where it
    # can be asked, the app's own mapping line where it cannot (see
    # Grid.window_evidence and the docstring above).
    how, wl = vm.window_evidence("Traduku", cursor=cur)
    assert "Traduku" in wl, f"{how} window evidence lacks the app window:\n{wl}"
    # frame size (resize(980,560) + server-side decoration titlebar).
    # KWin versions measure the frame differently (6.8: '980x588',
    # 6.3: '982x589' - decoration/border extents), so compare the width
    # numerically with a small tolerance instead of pinning one compositor's
    # string: the intent is 'the real window, roughly its designed size',
    # not a panel/1x1 placeholder (which is how a missing window shows up).
    m = re.search(r"(\d+)x(\d+)", wl[wl.index("Traduku"):])
    assert m, f"window present with unexpected size:\n{wl}"
    assert 970 <= int(m.group(1)) <= 992 and 550 <= int(m.group(2)) <= 610, \
        f"window size out of expected band: {m.group(0)}\n{wl}"
