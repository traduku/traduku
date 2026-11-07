"""E2E-9: Stage 6 - a GNOME user alone with the package.

Everything here is about the situation where nobody opens a terminal and
nobody reads the source: the package is installed, the session is a stock
GNOME, and the app has to say for itself that it exists, keep the promise
that it comes back at login, and be honest about the cues it cannot play.

Each test names the machine fact it was written against, because every one
of them was measured on ubuntu-gnome first and each contradicted an
assumption:

  * no icon in the panel (GNOME shows an SNI only with the appindicator
    extension), so a first-run greeting is the only thing that can teach the
    gesture;
  * only pw-play is installed (no paplay, no aplay), so "is there sound?"
    cannot be answered by guessing a player name;
  * the package installs /etc/xdg/autostart/traduku.desktop, so a menu tick
    that only looks at ~/.config/autostart is wrong twice over: it reads
    "off" while the app autostarts, and un-ticking removed a file that was
    not the reason it started.
"""
import json
import time

import pytest

_NOTIFY_WATCH = (
    "cat > /tmp/notify-watch.sh <<'SH'\n"
    "#!/bin/sh\n"
    "timeout 40 dbus-monitor --session "
    '"type=\'method_call\',interface=org.freedesktop.Notifications" '
    "> /tmp/notify.log 2>&1\n"
    "SH\n"
    "chmod +x /tmp/notify-watch.sh; rm -f /tmp/notify.log; "
)

USER_AUTOSTART = "/home/vm/.config/autostart/traduku.desktop"
PACKAGED_AUTOSTART = "/etc/xdg/autostart/traduku.desktop"


def _gnome_only(vm):
    if not vm.vm.endswith("-gnome"):
        pytest.skip("GNOME-specific: Plasma has no packaged autostart entry and "
                    "its own tray/first-run path (test_02 covers it)")


def _reset_autostart(vm):
    """Back to the packaged state: no user file, so the entry the package
    installs is the one in effect again."""
    vm.run(f"rm -f {USER_AUTOSTART}")


# ---------------------------------------------------------------------------
def test_first_run_tells_the_user_what_to_press(session):
    """The gesture is the whole interface, and nothing in the package says so.

    Observed on ubuntu-gnome: a fresh install autostarts, shows no icon
    (appindicator missing), and the user - who has no reason to believe the
    app is even running - never learns that Ctrl+C twice is the interface.
    The greeting is the only moment available, so its text is the product.
    """
    vm = session
    _gnome_only(vm)
    vm.run("pkill -x traduku; rm -f ~/.config/traduku/config.ini; true")

    # org.freedesktop.Notifications is a method call on gnome-shell's service,
    # so the only witness is the bus itself. Written as a script: an inline
    # `dbus-monitor ... &` over ssh holds the connection open until the
    # harness timeout, which is not the failure being looked for. The timeout
    # bounds the watcher so a red run cannot leak a monitor into the guest.
    vm.ses(_NOTIFY_WATCH + "setsid nohup /tmp/notify-watch.sh >/dev/null 2>&1 </dev/null & sleep 1")

    # An endpoint is configured, so the greeting must arrive without a modal
    # settings window: this app is autostarted unattended at login, and the
    # surest way to teach someone to switch autostart off is a dialog at login.
    vm.run("mkdir -p ~/.config/traduku; printf '%s\\n' '[General]' "
           "'ApiUrl=http://127.0.0.1:9/null' 'Model=m' 'SourceLang=auto' "
           "'TargetLang=ru' 'FirstRunDone=false' > ~/.config/traduku/config.ini")
    vm.ses("(setsid traduku > /tmp/first-run.out 2>&1 &) ; sleep 6")

    rc, log = vm.ses("cat /tmp/notify.log")
    rc2, out = vm.ses("cat /tmp/first-run.out")
    try:
        vm.run("pkill -x traduku; true")
    except Exception:
        pass
    assert "org.freedesktop.Notifications" in log and "Notify" in log, (
        "no notification was sent on the first run; the bus log was:\n"
        f"{log}\nstdout was:\n{out}")
    lowered = log.lower()
    assert "ctrl+c" in lowered or "ctrl c" in lowered, (
        "the first-run notification never names the gesture - a greeting that "
        f"does not say what to press teaches nothing:\n{log}")
    # ... and it must not ask the question the package already answered.
    assert "start at login" not in lowered and "automatically at login" not in lowered, (
        "the GNOME first run asks about autostart, which the package installs "
        f"(packaging/gnome/traduku.desktop): {log}")


def test_first_run_does_not_grab_the_desktop_when_it_is_configured(session):
    """No modal at login. Measured by asking the XWayland server for toplevels.

    With an endpoint configured there is nothing to fill in, so the first run
    must notify and stay out of the way. The KDE shell deliberately does open
    the settings dialog (it is also asking about autostart there); this shell
    starts from gdm's autologin with nobody watching the screen.
    """
    vm = session
    _gnome_only(vm)
    vm.run("pkill -x traduku; true")
    vm.run("mkdir -p ~/.config/traduku; printf '%s\\n' '[General]' "
           "'ApiUrl=http://127.0.0.1:9/null' 'Model=m' 'SourceLang=auto' "
           "'TargetLang=ru' 'FirstRunDone=false' > ~/.config/traduku/config.ini")
    vm.ses("(setsid traduku > /tmp/first-run2.out 2>&1 &) ; sleep 6")
    # The settings dialog is a QWidget window; on this session XWayland lists
    # it (xdotool saw the translation window in test_06).
    rc, out = vm.ses("DISPLAY=:0 XAUTHORITY=$(ls -1 /run/user/1000/.mutter-Xwaylandauth.* "
                     "2>/dev/null | head -1) xdotool search --name . getwindowname %a "
                     "2>/dev/null | tr '\\n' '|'")
    try:
        vm.run("pkill -x traduku; true")
    except Exception:
        pass
    names = out.lower()
    assert "settings" not in names or "traduku" not in names, (
        "a settings dialog opened unattended at login on a configured install: "
        f"windows were {out!r}")


# ---------------------------------------------------------------------------
def test_autostart_command_overrides_the_packaged_entry(session):
    """`--autostart=off` must stop the next login, not just tidy ~/.config.

    The tray menu is the other way in, and on this desktop the menu lives in a
    panel icon that needs a shell extension to be visible - so the command has
    to be able to finish the job by itself. Red check (measured while writing
    this): with the old implementation `off` deleted the user file, the status
    line still said the app would start, and the next login started it again.
    """
    vm = session
    _gnome_only(vm)
    rc, owner = vm.run(f"test -f {PACKAGED_AUTOSTART} && echo packaged")
    assert "packaged" in owner, f"no {PACKAGED_AUTOSTART} to override"

    _reset_autostart(vm)
    rc, out = vm.ses("traduku --autostart=status 2>&1; echo rc=$?")
    assert "rc=0" in out, f"--autostart=status failed: {out}"
    assert "autostart:" in out, f"no status line: {out}"
    assert "package" in out.lower(), (
        f"status does not say the entry comes from the package: {out}")

    rc, out = vm.ses(f"traduku --autostart=off 2>&1; echo rc=$?; "
                     f"cat {USER_AUTOSTART} 2>&1")
    assert "rc=0" in out, f"--autostart=off failed: {out}"
    assert "Hidden=true" in out, (
        "disabling left no shadowing entry, so the next login starts the app "
        f"anyway ({USER_AUTOSTART} was):\n{out}")

    rc, out = vm.ses("traduku --autostart=status 2>&1")
    assert "off" in out.lower(), f"still claims to start after off: {out}"

    rc, out = vm.ses("traduku --autostart=on 2>&1; traduku --autostart=status 2>&1")
    assert "enabled" in out.lower(), f"re-enabling did not take: {out}"

    # ... and it must answer with no session at all: the person who cannot
    # see the icon is often the person connected over ssh.
    rc, out = vm.run("env -u DISPLAY -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM "
                     "traduku --autostart=status 2>&1; echo rc=$?")
    assert "rc=0" in out and "autostart:" in out, (
        f"--autostart=status needs a display, which is the one thing the user "
        f"without a panel icon may not have: {out}")

    # A typo is an error, not a tray that never exits.
    rc, out = vm.ses("traduku --autostart=yes-please 2>&1; echo rc=$?")
    assert "rc=0" not in out and "status" in out.lower(), (
        f"--autostart=yes-please was not refused as a command: {out}")
    _reset_autostart(vm)


# ---------------------------------------------------------------------------
def test_backend_info_reports_the_sound_path(session):
    """"It is silent" must be distinguishable from "it is off" and "no theme".

    ubuntu-gnome has PipeWire and exactly one player: pw-play (measured:
    paplay and aplay are not installed). So the report has to name the player
    it found here rather than the one the developer's machine happens to have.
    """
    vm = session
    rc, out = vm.ses("traduku --backend-info 2>&1; echo rc=$?")
    assert "rc=0" in out, f"--backend-info failed: {out}"
    line = [l for l in out.splitlines() if l.startswith("audio:")]
    assert line, f"no audio: line, so a silent app cannot be diagnosed:\n{out}"
    value = line[0].split(":", 1)[1].strip()

    if value.startswith("off"):
        return                      # the user switched it off; nothing to check
    if "not found" in value or "none of" in value or "not installed" in value:
        # A refusal is allowed, but only a true one.
        rc, chk = vm.ses("for p in paplay pw-play aplay; do command -v $p; done")
        assert not chk.strip(), (
            f"the report says nothing can play sound, but these are installed: "
            f"{chk}\nreport: {value}")
        return

    player = value.split(" - ")[0].strip()
    rc, found = vm.ses(f"command -v {player}")
    assert found.strip(), f"the report names a player that is not installed: {value}"
    if " - " in value:
        path = value.split(" - ", 1)[1].strip()
        rc, ok = vm.ses(f"test -r {path} && echo readable")
        assert "readable" in ok, f"the report names a sound file that is not there: {value}"


def test_backend_info_reports_autostart(session):
    """The first question after a reboot is "did it come back?", and only a
    file on that machine answers it."""
    vm = session
    rc, out = vm.ses("traduku --backend-info 2>&1")
    line = [l for l in out.splitlines() if l.startswith("autostart:")]
    assert line, f"no autostart line:\n{out}"


# ---------------------------------------------------------------------------
def test_the_window_still_fits_the_screen_at_200_percent(session):
    """200% scaling is the default on a HiDPI laptop, not a setting to dig for.

    The window asked for 980x560 logical pixels unconditionally. At scale 2
    that is 1960x1120 device pixels: on this 1280x800 display the bottom of
    the window - the buttons - is off the panel, and a Wayland client cannot
    drag it back. Measured through the QMP screendump rather than the X
    screen, because the number that matters is the scanout, not what
    Xwayland tells a client.
    """
    import re
    import time

    vm = session
    sw, sh = vm.screen_size()
    assert sw >= 800, f"the display is {sw}x{sh}, too small to conclude anything"

    vm.run("pkill -x traduku; true")
    vm.start_app(extra_env=["QT_SCALE_FACTOR=2"])
    time.sleep(4)
    cur = vm.cursor()
    # Second launch asks the running instance to surface its window - the
    # same path a user reaches by clicking the (here: missing) icon twice.
    vm.ses("timeout 15 traduku >/dev/null 2>&1; true")
    rc, line = vm.journal_after(cur, grep="window shown")
    try:
        vm.run("pkill -x traduku; true")
    except Exception:
        pass
    m = re.search(r'window shown "[^"]*" (\d+)x(\d+) \((\d+)x(\d+) px\)', line)
    assert m, (
        "no 'window shown <title> WxH (WxH px)' line after surfacing the "
        f"window at 200%:\n{line}")
    lw, lh, dw, dh = (int(g) for g in m.groups())
    assert dw <= sw and dh <= sh, (
        f"window is {dw}x{dh} device px ({lw}x{lh} logical at scale 2) on a "
        f"{sw}x{sh} screen: the bottom of it is unreachable, and the user "
        "cannot move a Wayland window back onto the panel")
    assert (lw, lh) != (980, 560), (
        "the window kept its hard-coded 980x560 at 200% - fitToScreen was "
        "not consulted on show")


def test_the_launcher_entry_and_the_window_are_the_same_app(session):
    """GNOME pairs a window with its launcher by WM_CLASS/StartupWMClass.

    Measured on this guest: Qt gives WM_CLASS = "traduku", "traduku", and the
    shipped .desktop named no class at all, so the dash showed the launcher
    icon and a second nameless icon for the same app - the first complaint a
    GNOME user files, and one no unit test can see. The assertion compares
    the two files the compositor compares: what the window says and what the
    .desktop claims.
    """
    import time

    vm = session
    vm.run("pkill -x traduku; true")
    # xcb gives an X11 window with a readable WM_CLASS; under pure Wayland
    # there is no such property to read, and the app_id path is covered by
    # QGuiApplication::setDesktopFileName (see tests/unit/tst_cli.cpp).
    vm.start_app(extra_env=["QT_QPA_PLATFORM=xcb"])
    time.sleep(4)
    # A second copy asks the running one to surface its window.
    vm.ses("timeout 15 traduku >/dev/null 2>&1; true")
    time.sleep(3)
    # Selected by pid, not by title: gnome-shell wraps every XWayland window
    # in a mutter frame that carries the same title, and the frame's WM_CLASS
    # is "mutter-x11-frames" - matching on the title would have measured the
    # compositor's decoration and called it the app. Every traduku pid is
    # asked, because the autostart instance and the one this test started can
    # coexist, and picking head -1 once selected the windowless one.
    rc, out = vm.ses(
        "for p in $(pgrep -x traduku); do xdotool search --pid $p 2>/dev/null; done "
        "| while read id; do xprop -id $id WM_CLASS 2>/dev/null; done | head -1")
    try:
        vm.run("pkill -x traduku; true")
    except Exception:
        pass
    classes = [l.split("=", 1)[1].strip().strip('"').split('"')[0]
               for l in out.splitlines() if "WM_CLASS" in l]
    classes = [c for c in classes if c]
    assert classes, (
        "no Traduku toplevel with a WM_CLASS under XWayland; xprop said:\n" + out)
    wm_class = classes[0]

    rc, desktop = vm.run("cat /usr/share/applications/traduku.desktop")
    assert rc == 0, f"no launcher entry installed: {desktop}"
    claimed = [l.split("=", 1)[1].strip() for l in desktop.splitlines()
               if l.strip().startswith("StartupWMClass")]
    assert claimed, (
        f"{wm_class} is the window's class and the launcher names none: the "
        "dash shows two icons for one app")
    assert wm_class in claimed, (
        f"window says WM_CLASS={wm_class}, launcher claims {claimed[0]}")


def test_a_russian_session_defaults_to_russian(env):
    """A user who never opens the settings dialog still gets their own language.

    The default target language is the only place the session locale is read
    (Settings falls back to QLocale::system()), and a UTF-8 locale is not the
    interesting one: C.utf8 is what every earlier test ran under, so this is
    the first time the value is anything but the built-in "en". The mock
    echoes the language it was asked for, which is the app's answer rather
    than the harness's.

    ru_RU.UTF-8 is generated here because the image ships C, C.utf8 and
    en_US.utf8 only; locale-gen is idempotent, so this costs nothing after
    the first run.
    """
    import json
    import time

    vm = env
    # Two separate sudo calls on purpose: vm.sudo prefixes the string with
    # `echo vm | sudo -S`, so an `a || b` one-liner would run only `a` as root
    # and swallow b's failure behind a redirect.
    #
    # The recipe is distro-specific and both are measured, not guessed: the
    # Ubuntu guest takes `locale-gen <name>`, the Arch guest has no such
    # argument form - it reads /etc/locale.gen, where the line ships commented
    # out (measured: three commented ru_RU lines, locale-gen after uncommenting
    # one printed "ru_RU.UTF-8... done").
    rc, have = vm.run("locale -a | grep -ci '^ru_RU.utf8$'")
    if have.strip() == "0":
        if "ubuntu" in vm.vm or "debian" in vm.vm:
            vm.sudo("apt-get install -y locales")
            vm.sudo("locale-gen ru_RU.UTF-8")
        else:
            vm.sudo(r"sed -i 's/^#ru_RU.UTF-8 UTF-8/ru_RU.UTF-8 UTF-8/' /etc/locale.gen")
            vm.sudo("locale-gen")
        rc, have = vm.run("locale -a | grep -ci '^ru_RU.utf8$'")
    assert have.strip() != "0", (
        f"could not generate ru_RU.UTF-8 on {vm.vm}: neither the distro's "
        "locale-gen argument form nor /etc/locale.gen produced it - the test "
        "fails rather than skipping, because a skipped locale test is how "
        "'works for English users only' ships")

    def ask(locale):
        cfg = f"/tmp/config-{locale}.ini"
        vm.run(f"rm -f {cfg}; mkdir -p ~/.config/traduku; "
               f"printf '%s\\n' '[General]' 'ApiUrl=http://127.0.0.1:8931/v1' "
               f"'Model=mock' > {cfg}")
        return vm.ses(f"env LC_ALL={locale} TRADUKU_CONFIG={cfg} "
                      f"traduku --translate --text {json.dumps('hello world', ensure_ascii=False)} "
                      f"2>&1; echo rc=$?")

    rc, ru = ask("ru_RU.UTF-8")
    assert "rc=0" in ru, f"translation under the Russian locale failed:\n{ru}"
    assert "Russian" in ru, (
        "a Russian session did not ask for Russian; the mock was told:\n" + ru)
    rc, en = ask("C.utf8")
    assert "English" in en, (
        "the control: under C.utf8 the same build must ask for English - if it "
        f"also says Russian, the language is hard-coded and the comparison "
        f"above proved nothing:\n{en}")


def test_non_latin_text_survives_the_whole_path(env):
    """Cyrillic in, Cyrillic out: clipboard -> window -> back to clipboard.

    A locale is not the risk here (the guest runs C.utf8 and the app speaks
    UTF-8 end to end); the risk is a single QString::toLocal8Bit or a
    latin1()-typed byte array on the way, which turns Привет into ??????? and
    only ever shows up on a real desktop with a real IME.
    """
    vm = env
    text = "Привет, мир — перевод 42"
    vm.clipset(text)
    # ensure_ascii=False is the point: json.dumps' default escapes the word to
    # \uXXXX, and the test then measures whether the shell passes backslashes
    # through - not whether the app survives Cyrillic. (It cost a detour
    # chasing a "double escaping bug" that was this line.)
    rc, out = vm.ses(f"traduku --translate --text {json.dumps(text, ensure_ascii=False)} "
                     f"2>&1; echo rc=$?")
    assert "rc=0" in out, f"one-shot translation of non-latin text failed:\n{out}"
    assert "Привет" in out, (
        f"the non-latin text did not survive the request path:\n{out}")
