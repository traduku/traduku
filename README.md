# Traduku

Clipboard translator for Linux & Windows, powered by any OpenAI-compatible LLM
endpoint (Ollama, llama.cpp, vLLM, LM Studio, OpenAI, OpenRouter, etc).
Select text, press **Ctrl+C twice** — get a translation in a pop-up window and the clipboard.

- confidential and unlimited (when using a local AI)
- runs on **Linux** (KDE and GNOME, X11 and Wayland) and **Windows**.

The core is desktop-agnostic plain Qt; each desktop is a thin interface.

## Features

- Double Ctrl+C shortcut (`Ctrl+Alt+C` as a fallback).
- Two-panes window that streams and re-translates as you edit.
- Long texts are split into chunks by paragraphs and translated in parallel.
- Audio sounds for start / finish / failure
- a lot of destination languages, the default follows your locale.
- Plain INI config
- Console modes that work headless, over ssh, with no display:
  `--translate`, `--selftest`, `--backend-info`, `--autostart`,
  `--notify-test`.
- Tray icon (translates clipboard on left mouse click, menu on the right click
- Tray menu (translate now, destination language, launch on login, settings, quit)
- autostart

## Install

Packages for each platform are on the [release page](https://github.com/traduku/traduku/releases).

| platform | package | install |
|---|---|---|
| Debian / Ubuntu, Plasma | `traduku_<ver>_amd64.deb` | `sudo apt install ./traduku_<ver>_amd64.deb` |
| Debian / Ubuntu, GNOME | `traduku-gnome_<ver>_amd64.deb` | `sudo apt install ./traduku-gnome_<ver>_amd64.deb` |
| Arch / CachyOS, Plasma | `traduku-<ver>-1-x86_64.pkg.tar.zst` | `sudo pacman -U ./traduku-<ver>-1-x86_64.pkg.tar.zst` |
| Arch / CachyOS, GNOME | `traduku-gnome-<ver>-1-x86_64.pkg.tar.zst` | `sudo pacman -U ./traduku-gnome-<ver>-1-x86_64.pkg.tar.zst` |
| Windows 10/11 (x64) | `traduku-<ver>-win64-portable.zip` | unzip anywhere, run `traduku.exe` |

Arch packages are signed. The public key ships with the release (`packaging/arch/traduku-release.asc`); trust it once:

```sh
gpg --show-keys traduku-release.asc    # fingerprint 6412BB09BC5E27F2D0FE2EE7DE28065CC5A065AC
sudo pacman-key --add traduku-release.asc
sudo pacman-key --lsign-key DE28065CC5A065AC
```

The key is published by the same repository as the packages, so confirm the
fingerprint over a second channel if that matters to you. Another variant is to build the Traduku yourself.


To veryfi the downloaded files:
```sh
sha256sum -c SHA256SUMS
```

The Windows package is portable: `windeployqt` plus the MinGW runtime plus the
`offscreen` platform plugin, so it runs on a machine with no Qt.
Double-clicking `traduku.exe` starts the tray app — by design no window appears.

### Settings

1. tray icon → **Settings…**
2. Set the **API address** (`http://localhost:11434/v1` for Ollama), the model
   and the destination language. *Test API* checks reachability.
3. Select text, press **Ctrl+C twice**. The finished translation can also
   replace the clipboard (a setting).


## Native tools used in different OS

| | Plasma (X11 / Wayland) | GNOME on Wayland | Windows 10/11 |
|---|---|---|---|
| gesture | XI2 raw · X RECORD helper (XWayland) | evdev `/dev/input/event*` | `WH_KEYBOARD_LL` hook |
| shortcut fallback | KGlobalAccel (`Ctrl+Alt+C`) | none | `Ctrl+Alt+C` |
| clipboard | Qt (X11) · `wl-clipboard` | `wl-clipboard`, X11 selection on an X11 session | Qt (win32) |
| notifications | KNotification | freedesktop D-Bus | tray balloon (`Shell_NotifyIcon`) |
| tray | `QSystemTrayIcon` (SNI) | `QSystemTrayIcon` — needs an SNI host (pre-installed in Ubuntu) | `QSystemTrayIcon` |
| autostart | `~/.config/autostart` | packaged `/etc/xdg/autostart` | `HKCU\…\CurrentVersion\Run` |
| config | `~/.config/traduku/config.ini` | same | `%LOCALAPPDATA%\traduku\config.ini` |
| sounds | freedesktop theme via `pw-play`/`paplay`/`aplay` | same | `%WINDIR%\Media\*.wav` via `QMediaPlayer` |

Consequences for the Gnome version:
1. `wl-clipboard` is needed to access the clipboard.
2. permission is needed to listen the keyboard for the shortcut
    - solved as a `uaccess` udev rule - an ACL for Traduku (gone when the session ends).
    - check with `getfacl /dev/input/eventN` (expect `user:<you>:rw-`); the annotated rule is
  [`packaging/udev/70-traduku-uaccess.rules`](packaging/udev/70-traduku-uaccess.rules),
3. The GNOME tray icon is drawn by a StatusNotifier host:
    - Ubuntu ships one
    - Plain GNOME needs the `appindicator` shell extension enabled.
    - Without StatusNotifier host Traduku runs tray-less; everything else keeps working.


## Command line

```
traduku                       # tray app
traduku --translate           # clipboard -> stdout, streaming
traduku --translate --text "hi"
traduku --selftest [--live]   # verify an install
traduku --backend-info        # shows the environment information
traduku --notify-test         # show test notification through the desktop service
traduku --autostart=on|off|status
traduku --config <path>       # run with a config in an unusual place
```

`--text` needs `--translate`; `--live` only means something with `--selftest`.
One-shot modes exit 0 on a translation, 1 on empty input or a failed request.

## Configuration

`~/.config/traduku/config.ini` (`%LOCALAPPDATA%\traduku\config.ini` on
Windows), plain INI under `[General]`. Override order, highest first:
`--config <path>` → `$TRADUKU_CONFIG` → the XDG default.

| key | default | what it does |
|---|---|---|
| `ApiUrl` · `ApiKey` · `Model` | `http://localhost:11434/v1` · *(empty)* · `llama3.1` | the OpenAI-compatible endpoint (`<ApiUrl>/chat/completions`) |
| `TargetLanguage` | from locale | destination language code |
| `PressInterval` | `0.5` | seconds in which the second Ctrl+C counts |
| `GlobalShortcut` | `Ctrl+Alt+C` | fallback shortcut (press twice) |
| `CopyResult` | `true` | put the translation back on the clipboard |
| `AutoTranslate` · `AutoTranslateDelay` | `true` · `0.6` | re-translate the edited paragraphs after the source pane settles |
| `MaxWholeTextChars` | `6000` | at or under this, one whole-text request (document mode) instead of per-chunk requests |
| `MaxParallel` | `4` | concurrent chunk requests |
| `SoundsEnabled` | `true` | master mute |
| `SoundsOnStart` · `SoundsOnFinish` | `true` | the start cue; the finish cue (`SoundsOnFinish` also covers failure, because "it ended" arrives as success or as failure) |
| `DisableReasoning` | `true` | ask the model to skip its hidden reasoning channel |
| `ReasoningOffOptions` | vLLM + OpenAI preset | JSON merged when that switch is on |
| `ExtraOptions` | *(empty)* | JSON object merged into the request body last |
| `UseInputCapture` · `PortalKnownBroken` | `false` · `false` | Wayland InputCapture opt-in / portal known-broken escape |

Reasoning models answer the same prompt several times slower because they first
emit a private chain of thought that gets discarded. The default
`ReasoningOffOptions` sends:

```ini
ReasoningOffOptions={"chat_template_kwargs":{"enable_thinking":false},"reasoning_effort":"none"}
```

`--backend-info` prints which mode you are in; a malformed
`ExtraOptions` fails rather than silently translating with reasoning on.

Environment variables (for testing reasons):
- `TRADUKU_CONFIG` (config file)
- `TRADUKU_CLIPBOARD` (`wl|x11|qt`);
- `TRADUKU_TRAY` (`qt|none`).

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md);

## Build & test

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # -DLT_SHELL=gnome for the GNOME shell
cmake --build build
ctest --test-dir build --output-on-failure
./install.sh                                        # into ~/.local, incl. desktop data
```

`make help` lists the wrapper targets: `build`, `test`, `selftest`, `install`
(host), `x11test`, `guest`, `e2e`, `cycle` (the VM grid), `up`/`down`/
`scratch`/`reset`/`ssh`/`shot` (the test guest). They are aliases — Ninja owns
code, content hashes own packages, so a target cannot go green on a stale
artifact.

**Build deps:** Qt 6.4+ (Core Gui Widgets Network Test Core5Compat DBus,
Multimedia on Windows), CMake ≥ 3.21, C++17. The KDE shell adds KF6
(Notifications, GlobalAccel, DBusAddons, Config, I18n); the X11 layer adds
Xlib/XInput/XRecord (`x11`, `xtst`, `xi` dev packages). Both layers are
switchable (`-DLT_BUILD_KDE=OFF`, `-DLT_BUILD_X11=OFF`); the GNOME shell links
no KF6 at all, which is checked by `ldd` in the grid, not by a promise.

**Windows:**

```
cmake -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
cmake --build build-win && ctest --test-dir build-win
packaging/windows/build-portable.sh
```

Qt 6.8.3 win64_mingw + MinGW 13.1. No PkgConfig, no X11, no
KF6 — those layers are excluded by platform, and CMake says so rather than
pretending they were not asked for. The suite size differs per OS by design:
41 test rows build on Linux, 39 on Windows, plus 5 shell gates — 46 on Linux,
44 on Windows; 7 rows exist only on Linux (X11, evdev, XDG autostart, FDO), 5
only on Windows (the real hook, the real registry hive).

**What is measured where.** The e2e grid (real desktops in VMs, real keys)
last ran end to end on the 0.3.0 packages; the 0.4.0 Linux packages are built
and container-gated (`ctest` over Xvfb + `--selftest`), and the Windows shell
was walked by hand on a live Windows 11 desktop — the checklist, the findings
it produced and the fixes pinned against them are ROADMAP.md §7.7–§7.8.

Tests are per module and test-first: unit tests with no sockets, integration
tests against an in-process fragmented mock SSE server, offscreen UI tests, a
real-X gate (`make x11test` runs the same suites against Xvfb so the X
clipboard and key monitor cannot go green without an X server), per-VM e2e runs
that install the actual package and press real keys
(`make e2e VM=ubuntu-gnome`), and `traduku --selftest [--live]` for users.

Mock LLM Server:

```sh
./tools/mock-openai.py 8931 &                # streaming OpenAI-compatible mock
cat > /tmp/mock.ini <<'INI'
[General]
ApiUrl=http://127.0.0.1:8931/v1
Model=test-model
TargetLanguage=de
INI
traduku --config /tmp/mock.ini --translate --text "Hello"
# [MOCK -> Deutsch (German)] Hello
```

The mock answers as SSE deltas, so this exercises the streaming path, the
prompt and the language name without a model (verified against the packaged
`traduku.exe`).

## Troubleshooting

- **Translation feels slow** — check `reasoning:` in `--backend-info`; a model
  streaming a hidden channel is the usual cause (see Configuration).
- **"Clipboard is empty" on Wayland** — install `wl-clipboard`; if you suspect
  the reader rather than the clipboard, `TRADUKU_CLIPBOARD=x11
  traduku --backend-info` prints the chosen reader or the reason X refused.
- **Double Ctrl+C does nothing** — `--backend-info` names the listener it
  picked, and the journal line `traduku: ... key listener active` shows it
  started. For evdev, check the ACL (`getfacl /dev/input/eventN`). With no
  observer available the Plasma fallback engages and its shortcut appears in
  the tray tooltip; on GNOME and Windows there is no fallback, so
  `--backend-info` is the whole answer.
- **No sounds** — a sound file and *some* player must exist; `--backend-info`
  prints `audio: <profile>: <player>` plus one line per cue, and a cue switched
  off answers `audio <cue>: off (SoundsOnStart=false)` rather than a path.
  Sounds are best-effort.
- **kf.i18n warnings** — the app ships plain-English source strings and no
  catalogs; translations can be added per module later.

## License

GPL-3.0-or-later — see [LICENSE](LICENSE).
