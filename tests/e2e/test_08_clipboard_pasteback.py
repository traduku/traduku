"""E2E-8: the finished translation is written back to the clipboard.

CopyResult=true (the default behaviour users rely on: Ctrl+V pastes the
translation). After the gesture the clipboard must go from the source text to
the streamed result: `[MOCK -> en] <source>` — a value the DE apps receive:
Klipper/any Wayland paste target on a Wayland session, any X11 client on the
native X11 Plasma of debian-kde.

Seeded and read through the session's own clipboard tool (Grid.clip_tool), so
"the clipboard still holds the seed" means the app really did not write, not
that the harness asked the wrong compositor.

Red check: with CopyResult=false the clipboard must keep the source.
"""
from conftest import clip_until

SEED = "Paste me back 77"


def test_translation_written_back_to_clipboard(env):
    vm = env
    vm.clipset(SEED)
    ok, why = vm.focus_fresh_term()
    assert ok, ("no foreign window took the focus; the helper said: " + str(why))
    # KGA pair on Wayland; on the X11 session the same key sequence reaches the
    # app's passive X RECORD listener (test_05).
    vm.double_ctrl_alt_c()
    ok, got = clip_until(vm, "[MOCK -> ", timeout=25)
    assert got.strip().startswith("[MOCK -> ") and SEED in got, \
        f"clipboard was not replaced with the result: {got!r}"
