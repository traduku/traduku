# Traduku

A modular, streaming clipboard translator for Linux desktops, powered by any
OpenAI-compatible LLM endpoint (vLLM, llama.cpp server, Ollama, LM Studio,
OpenAI, OpenRouter, …). Select text, press **Ctrl+C twice** — a
DeepL-style window opens and the translation **types itself out live** as
the model streams it.

Rewritten from the ground up out of the `kde-translate` prototype: the core
is desktop-agnostic (plain Qt), KDE/Plasma is just one pluggable shell, and
a GNOME shell only needs to implement the small backend interfaces.

## What works on which desktop

Every row is a run, not an intention. The evidence column names the machine
and the suite that produced it; `traduku --backend-info` on the machine in
front of you prints the same decisions for your session.

### Which package is ready where

A ✅ is a package built from this tree, installed into a **booted** guest of
that distro and desktop, and passed the end-to-end suite there - the numbers
are that run. A ⚠️ means the package builds and its own gates pass (the
container ctest, the on-guest `--selftest`), but **no desktop of that
combination has been booted here**, so nobody has watched a window appear on
it. That is a weaker claim and is written as one.

| distro | GNOME | Plasma |
|---|---|---|
| **Debian 13 (trixie)** | ✅ `traduku-gnome_0.3.0-1_amd64.deb` — 20 passed / 3 skipped, 40.6 s. GNOME on **Xorg**, and no GNOME extensions at all, so the tray test skips naming the missing appindicator host | ✅ `traduku_0.3.0-1_amd64.deb` — 17 passed / 6 skipped, 91.1 s. Plasma on **X11** (`kwin_x11`, Xorg on vt2) |
| **Ubuntu 25.10** | ✅ `traduku-gnome_0.3.0-1_amd64.deb` — 21 passed / 2 skipped, 43.9 s. GNOME Shell 49 on Wayland, tray hosted | ✅ `traduku_0.3.0-1_amd64.deb` — 18 passed / 5 skipped, 103.0 s. Plasma on Wayland (`kwin_wayland` + Xwayland) |
| **Arch** | ⚠️ `traduku-gnome-0.3.0-1-x86_64.pkg.tar.zst` is built and gated (37/37 in the GNOME tree, `--selftest` on the guest), but **no Arch GNOME session has been booted** - nobody has run the e2e suite on it | ✅ `traduku-0.3.0-1-x86_64.pkg.tar.zst` — 18 passed / 5 skipped, 154.3 s. Plasma on Wayland |

Two cells are worth a sentence because they are the ones an assumption would
have got wrong:

- **Debian's GNOME is on Xorg, not Wayland** (`XDG_SESSION_TYPE=x11`, no
  Xwayland), so that flavour reads the clipboard through the X11 selection and
  still gets its gesture from evdev. Both are decisions the app makes per
  session, printed in `--backend-info`; neither is compiled in.
- **Debian's Plasma is on X11 too.** Getting it into the grid is what found
  two real bugs - the X11 `TARGETS` reader truncating every list to its first
  item, and the harness waiting for a Wayland socket that this desktop never
  creates.

### The seams, per session

| session | package | gesture | clipboard | hotkey | notifications | tray | autostart | evidence |
|---|---|---|---|---|---|---|---|---|
| **GNOME on Wayland** | `traduku-gnome` | evdev `/dev/input/event*` (uaccess ACL) | `wl-clipboard` | evdev | freedesktop D-Bus | Qt SNI (Ubuntu hosts it) | packaged `/etc/xdg/autostart` | `ubuntu-gnome`, ready line `(gnome, wayland; clipboard=wl-clipboard hotkey=evdev tray=yes)`, e2e **21 passed / 2 skipped** |
| **GNOME on X11** | `traduku-gnome` | evdev | **X11 selection** (`XConvertSelection`) | evdev | freedesktop D-Bus | none → runs tray-less and says so | packaged `/etc/xdg/autostart` | `debian-gnome`, ready line `(gnome, x11; clipboard=x11 hotkey=evdev tray=no)`, e2e **20 passed / 3 skipped** |
| **Plasma on Wayland** | `traduku` | X RECORD on XWayland (helper process) | `wl-clipboard` | X RECORD | KNotification | Qt SNI (Plasma hosts SNI) | `~/.config/autostart`, written when you answer the first-run question | `arch-kde` and `ubuntu-kde`, ready line `(kde, wayland; clipboard=wl-clipboard hotkey=x11-record tray=yes)`, e2e **18 passed / 5 skipped** each |
| **Plasma on X11** | `traduku` | XI2 raw on the X server | **X11 selection** | XI2 raw | KNotification | Qt SNI | as above | `debian-kde`, ready line `(kde, x11; clipboard=x11 hotkey=x11-xi2 tray=yes)`, e2e **17 passed / 6 skipped** |
| **Bare X11 / other DE** | either | XI2 raw (RECORD fallback) | X11 selection, Qt last | XI2 raw | freedesktop D-Bus | Qt SNI if a host exists | XDG autostart (XFCE, i3, … honour it) | unit/integration only (`make x11test`, 37 tests against a real X server) |
| **Windows 10/11 (win64)** | `traduku.exe` (MinGW, GUI subsystem) | `WH_KEYBOARD_LL` hook, passive | Qt (win32 clipboard) | — (the hook *is* the gesture; `RegisterHotKey` was rejected: it intercepts the copy) | tray balloon via `Shell_NotifyIcon` | `QSystemTrayIcon` | `HKCU\…\CurrentVersion\Run` | built 2026-11-08, Qt 6.8.3 + MinGW 13.1: **41/41** ctest rows (9 of them exist only here), `--translate`/`--selftest --live` measured against a mock SSE endpoint, the Run value verified by an independent reader. **Walked by hand on a live Windows 11 desktop**: tray icon at start, left-click translates the clipboard, right-click menu, balloon, `Ctrl+C`+`Ctrl+C` — and two defects came out of that walk, neither visible to any test (a mojibake tray label, the start cue silent while the finish cue plays). See ROADMAP.md §7.7–§7.8 |

The hotkey column is a measurement, not a menu: on the same image the listener
can be `x11-xi2` or the X RECORD helper, and the choice is printed by
`--backend-info` and in the `traduku: ready (...)` journal line rather than
guessed from the distro name (`hotkey chosen:` and `modeName()` report the
listener that is actually running - they used to say `x11-xi2` in both cases).

Two things this table deliberately does not hide:

- **No application icon ships yet.** Both `.desktop` entries use the themed
  name `accessories-dictionary` (`dpkg -L traduku-gnome | grep icons` → 0
  files). On a theme without that name the launcher shows a generic icon.
  What *is* verified is identity, not beauty: `WM_CLASS` is `traduku` and
  `StartupWMClass` matches, so gnome-shell pairs the window with its entry
  instead of showing a second, nameless one.
- **The GNOME tray icon needs a StatusNotifier host.** Plain GNOME has none;
  Ubuntu ships one. See [The tray icon on GNOME](#the-tray-icon-on-gnome).

## Install

Packages for both desktops are on the [release page](https://github.com/traduku/traduku/releases);
install the package of **your desktop** — the tray app, the desktop entry, the
sounds and (GNOME) the uaccess rule:

| platform | download | install it |
|---|---|---|
| Arch / CachyOS | `traduku-0.3.0-1-x86_64.pkg.tar.zst` (Plasma) or `traduku-gnome-0.3.0-1-x86_64.pkg.tar.zst` (GNOME) | `sudo pacman -U ./traduku-0.3.0-1-x86_64.pkg.tar.zst` |
| Debian / Ubuntu | `traduku_0.3.0-1_amd64.deb` (Plasma) or `traduku-gnome_0.3.0-1_amd64.deb` (GNOME) | `sudo apt install ./traduku_0.3.0-1_amd64.deb` |

The Arch packages are signed, and on Arch that is not decoration: with the
default `SigLevel = Required`, `pacman -U` of a package whose signature the
keyring cannot verify answers `package missing required signature` — it does
not install and it does not ask. So the public key ships with the release
(`traduku-release.asc`, also in the repo under `packaging/arch/`) and you trust
it once:

```sh
gpg --show-keys traduku-release.asc   # fingerprint 6412BB09BC5E27F2D0FE2EE7DE28065CC5A065AC
sudo pacman-key --add traduku-release.asc
sudo pacman-key --lsign-key DE28065CC5A065AC
sudo pacman -U ./traduku-0.3.0-1-x86_64.pkg.tar.zst
```

That is trust-on-first-use: the key is published by the same repository as the
packages, so confirm the fingerprint over a second channel before signing it if
that matters to you. To check a download without trusting anything:

```sh
sha256sum -c SHA256SUMS                              # names every package + the source tarball
gpg --verify traduku-0.3.0-1-x86_64.pkg.tar.zst.sig
```

Building it yourself (`packaging/arch/mkrelease.sh` for Arch, `dpkg-buildpackage`
with `packaging/debian/` for Debian/Ubuntu) needs no key at all, and is the
path to take if you would rather not add one.

One source, two flavours, because the two shells deliberately do not share a
dependency set: the GNOME shell links **no KF6 at all** (GNOME ships none),
the Plasma shell uses KF6 for notifications and global shortcuts. Both
install `/usr/bin/traduku` and the same `.desktop`, so they `Conflict` —
a machine runs the flavour of its desktop. That split is what makes the
"GNOME shell has no KF6" rule checkable rather than a promise: on a GNOME
install, `ldd /usr/bin/traduku` must print no KF6 (the grid asserts exactly
that, via `pacman -Q`/`dpkg -S` plus `ldd`).

Dependencies that are load-bearing, and why:

- **`wl-clipboard` is a hard dependency of `traduku-gnome`** and only a
  recommendation of the Plasma flavour. The GNOME shell reads the clipboard
  through `wl-paste`, and Qt's own `QClipboard` cannot be used from a
  background process on Wayland (measured: it returns empty with no error).
  Without the tool the app still starts and says which package to install,
  but it cannot do its job. On Plasma the desktop owns the clipboard service,
  so Qt's client is enough.
- **`libxi`/`libxtst`/`libx11`** carry the passive key monitor and the X
  clipboard reader. They are needed even on a Wayland session, because
  XWayland is where the keys of an X11-focused window are observable.

For development, without root:

```bash
./install.sh          # into ~/.local, including desktop data
make help             # the entry point for build / test / guest / e2e targets
```

## The `/dev/input` rule (and why `input` group is the wrong fix)

The advertised gesture is literally Ctrl+C twice. A Wayland compositor never
hands foreign key presses to a client — so on GNOME the app observes the
keyboard by **reading** `/dev/input/event*`: no grab, no injection, normal
copying untouched. That read needs permission on the device node, and there
are two ways to grant it:

- **`uaccess` (what the package does).** An ACL is placed on the node for the
  user of the *active seat session* only: a locked screen, another seat or
  another user get nothing, and the ACL goes away when the session does.
  Measured on the installed `traduku-gnome` package:

  ```console
  $ getfacl /dev/input/event1
  # group: input
  user::rw-
  user:vm:rw-        ← the ACL, from /usr/lib/udev/rules.d/70-traduku-uaccess.rules
  group::rw-
  ```

- **Adding the user to the `input` group (the usual advice, and wrong).**
  Group membership is account-wide and permanent: every process you ever run
  — a browser tab, a build script, anything — can read every keyboard on the
  machine forever, including the one you type your password into on a locked
  screen. It is a sledgehammer for a pushpin.

The rule is installed by the packages and applies to devices already present
at install time (the Arch split package runs a `.install` hook, the `.deb`
has a `postinst`), so it does not need a reboot. If you want to reason about
it yourself — or narrow it to one laptop keyboard with
`ENV{ID_VENDOR_ID}=="…"` — the annotated rule is
[`packaging/udev/70-traduku-uaccess.rules`](packaging/udev/70-traduku-uaccess.rules).
It documents a trap worth knowing before you debug one of your own: the file
must sort **before `71-seat.rules`**, because `TAG=="uaccess"` is evaluated
against the tags collected so far — installed as `99-*` it sets a tag nobody
reads, `getfacl` stays at `root:input 0660`, and `udevadm info` cheerfully
reports `TAGS=:uaccess:` while everyone blames the trigger.

Checking, revoking and the `udevadm test --extra-rules-dir` recipe are in that
file's comments. `traduku --backend-info` tells you whether the gesture
actually got the device: `gesture: evdev /dev/input/event*` plus
`hotkey chosen: evdev` means it opened a keyboard; anything else names the
path it fell back to.

## The tray icon on GNOME

`traduku-gnome` lists the extension as **`Suggests`**, not `Recommends`, on
purpose: the app runs, gestures, translates, writes the clipboard and posts
notifications with no tray host at all (measured on a guest with the extension
disabled — Ubuntu ships `gnome-shell-extension-appindicator`, plain GNOME does
not). The extension only decides whether the icon is *drawn*. A user who never
touches the tray should not be offered a shell extension; a user who wants the
icon installs it and enables it:

```bash
gnome-extensions list | grep -i indicator        # the uuid is distro-specific:
                                                 # ubuntu-appindicators@ubuntu.com
                                                 # on Ubuntu, appindicatorsupport@
                                                 # rgcjonas.gmail.com on Debian
gnome-extensions enable <that-uuid>              # then log out and back in:
                                                 # gnome-shell scans the extension
                                                 # directories at boot
```

and the tray menu — translate now, destination language, autostart, settings,
quit — appears. With no tray host, the first-run notification explains this
instead of silently omitting an icon.

## Features

- **Double Ctrl+C** launches translation of the clipboard.
  - Passive: it observes keys without grabbing them, so ordinary copying is
    never intercepted. Which observer is used depends on what the session
    makes observable (X RECORD / XI2 raw on X11 and XWayland, `/dev/input`
    with the uaccess ACL on GNOME's Wayland), and
    `--backend-info` names the one it picked.
  - Layout-independent: it matches the **physical** C key by keycode, so a
    Cyrillic or Greek layout changes nothing. The XKB modifier probe covers
    native-Wayland focus.
  - Fallback: a registered global shortcut (default `Ctrl+Alt+C`, press
    twice) via `KGlobalAccel`; pair detection reuses the same tested
    detector as the passive path.
- **Streaming result window** (`Original | Translation` side by side):
  source on the left, the LLM's answer appended live on the right, language
  picker, *Copy translation*, *Settings…*, status line. Long texts are no
  problem — the window scrolls, tray tooltips never could. The window fits
  the screen at 200 % scaling (aspect preserved, compositor margin respected).
- **Audio progress cues**: a start chime when the gesture fires, a
  completion sound when the translation is ready, an error sound on failure
  (freedesktop theme; toggleable in Settings). You can follow the process
  without watching the screen. `--backend-info` names the player it found
  (`audio: pw-play - /usr/share/sounds/freedesktop/stereo/complete.oga`) or
  the precise reason there is none — it does not guess `paplay` on a PipeWire
  box that has only `pw-play`.
- **Configurable endpoint** (address / API key / model), destination
  language, double-press window, clipboard behaviour — stored as a plain
  INI in `~/.config/traduku/config.ini`, editable from any DE.
- **Clipboard**: `wl-clipboard` (Wayland), X11 (`XConvertSelection`, target
  and charset negotiated with the offering client, legacy `TEXT` decoded by
  its real charset) or Qt — selected at runtime, overridable with
  `TRADUKU_CLIPBOARD=wl|x11|qt`. A forced backend that cannot work in your
  session is honoured only where it can; where it cannot, `--backend-info`
  says why instead of silently ignoring the knob.
- **Launch on Login** via XDG autostart (KDE, GNOME, XFCE all honour it),
  controllable without a desktop: `traduku --autostart=on|off|status`. The
  GNOME flavour ships a system entry in `/etc/xdg/autostart`; turning it off
  writes the user-side shadow the spec prescribes, and never rewrites the
  packaged file. Reading the state does not write it.
- One-shot CLI: `traduku --translate [--text "..."]` streams the
  translation to stdout — scripts, pipes, tests. Console modes answer over
  a plain `ssh` session with no display at all.

## Architecture

```
src/core/        pure Qt, no DE: settings (INI), languages, SSE streaming
                 parser, LLM client (stream=true), translation engine,
                 double-press detector, shortcut monitor, sound player,
                 text targets (MIME/atom/charset negotiation shared by both
                 clipboard readers)
src/interfaces/  IClipboard · IHotkeyBackend · INotifier · IAutostart ·
                 IGlobalShortcut          (the DE-portability seam)
src/platform/qt/ QtClipboard (X11) · XdgAutostart (XDG autostart spec,
                 system entries included) · QtTray (QSystemTrayIcon, Qt's
                 generic unix SNI client) · SessionInfo
src/platform/wl/ WlClipboard (wl-paste/wl-copy) + clipboard factory
                 (priority: wl-clipboard → X11 → Qt, per what the session has)
src/platform/x11/ X11HotkeyBackend (XI2 raw / RECORD, passive monitor) ·
                 X11Clipboard (selection owner + reader, X server discovery
                 through the Xwayland that a Wayland session started) ·
                 traduku-record-keys (the key emitter the monitor spawns)
src/platform/evdev/ EvdevHotkeyBackend (/dev/input, the literal Ctrl+C+C
                 on Wayland — reads events, never grabs them)
src/platform/kde/ KdeNotifier (KNotification) · KdeGlobalShortcut
                 (KGlobalAccel)
src/ui/          TranslatorWindow · SettingsDialog   (Qt Widgets only)
src/app/         AppController (composition root, DE-agnostic) · SelfTest ·
                 cli (one parser and one one-shot path for both shells) ·
                 AppFlavour (shared policy: CLI/QPA environment, first-run
                 plan, the "I am up" line, autostart command)
src/app/kde/     traduku main (KDE wiring)
src/app/gnome/   traduku main (GNOME wiring, no KF6 anywhere)
```

`AppController` connects the lifecycle — gesture → clipboard read → engine
streams → window updates → clipboard write / sounds / notifications. Both
shells build that same struct out of the same parts; nothing in `src/core`
or `src/ui` knows which desktop it is on. `traduku --backend-info` prints
which backend was chosen for each seam and why, on the machine you are on —
produced by the same decision path the running app takes, not recomputed
beside it.

## Build & test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure   # 37 unit/integration suites
./install.sh                                 # into ~/.local, incl. desktop data
```

`make help` lists the wrapper targets (`build`, `test`, `x11test`, `guest`,
`e2e`, `cycle`, `reset`, …). They are aliases only — Ninja owns code, content
hashes own packages, so a target can never be green on a stale artifact.

Build deps: Qt 6.4+ (Core Gui Widgets Network Test), CMake ≥ 3.21, a C++17
compiler; KDE layer: KF6 (StatusNotifierItem, Notifications, GlobalAccel,
DBusAddons); X11 layer: Xlib + XInput + XRecord (`x11`, `xtst`, `xi` dev
packages). X11/KDE layers are optional at configure time
(`-DLT_BUILD_X11=OFF`, `-DLT_BUILD_KDE=OFF`).

### On Windows

```
cmake -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
cmake --build build-win
ctest --test-dir build-win
```

Qt 6.8.3 win64_mingw + MinGW 13.1 (either MSVC or MinGW works; the run above
is the MinGW one), CMake and Ninja on `PATH`. No PkgConfig, no X11, no KF6:
those layers are excluded by platform and CMake says so instead of pretending
they were never asked for. The count differs per OS by design — 41 rows here,
`tst_xdgautostart` is not registered because `XDG_CONFIG_HOME` is a variable
Windows does not read, and 9 rows exist only on Windows (the real hook, the
real registry hive). See ROADMAP.md §7 for what is measured and what still
waits for a live desktop.

**To double-click the exe, deploy the runtime once.** `build-win/bin/traduku.exe`
imports `Qt6Core.dll` and friends, which the loader finds through `PATH` — so it
starts in a shell where Qt is on `PATH`, and from Explorer it gets "the code
execution cannot proceed because Qt6Core.dll was not found" (`0xc0000135`) before
a single line of our code runs; no message of ours can make that dialog kinder:

```
packaging/windows/deploy-runtime.sh build-win/bin
```

That is `windeployqt` plus the three MinGW DLLs by name plus `qoffscreen` —
without it `--selftest`, the command for proving an install and the only one a
session with no desktop can run, dies on Qt's own "could not find the Qt platform
plugin offscreen". The portable zip deploys through this same script; run it by
hand only if you want the build directory clickable. Double-clicking then starts
the tray app: by design no window appears, the icon goes to the tray.

Every module was written test-first (see ARCHITECTURE.md and the commit history):
unit tests for each core piece, integration tests over an in-process
fragmented mock SSE server, offscreen UI tests, a real-X gate
(`make x11test` runs the same 37 tests against Xvfb, so the X clipboard and
key monitor cannot go green without an X server), per-VM end-to-end runs that
install the actual package and press real keys
(`make e2e VM=ubuntu-gnome`), plus `traduku --selftest [--live]` for users to
verify an install.

## Usage

1. `traduku &` → tray icon appears (see
   [The tray icon on GNOME](#the-tray-icon-on-gnome) if it does not).
2. Tray → *Settings…* → API address (e.g. `http://localhost:11434/v1`),
   model, destination language. *Test API* checks reachability.
3. Select text → press **Ctrl+C twice quickly** → the window opens
   and streams the translation; start/finish sounds mark the boundaries.
   When enabled, the finished translation also replaces the clipboard.
4. Tray: translate now, destination language, autostart, settings, quit.
   Window: pick destination language (persisted), copy, settings.

The tray is not the only way in — these work headless, over ssh, with no
display:

```bash
traduku --backend-info              # every seam, its backend, and the reason
traduku --translate --text "hi"     # one-shot, streaming to stdout
traduku --autostart=status          # on | off | status, XDG entry aware
traduku --selftest --live           # verify an install against the configured endpoint
```

## Configuration

Settings live in **`~/.config/traduku/config.ini`** (XDG; the same tree holds
`~/.config/autostart/`), written by the settings dialog and watched for
external edits. A single `~/.traduku.ini` was rejected: v0.2.0 users already
have the XDG file, and the directory keeps growing siblings anyway.

Three levels of override, highest first:

```bash
traduku --translate --text "hi" --config /etc/traduku/server.ini   # explicit
TRADUKU_CONFIG=~/work/staging.ini traduku                            # per-shell
traduku                                                            # XDG default
```

Keys beyond address / key / model that are worth knowing:

| key | default | what it does |
|---|---|---|
| `DisableReasoning` | `true` | asks the model to skip its hidden reasoning channel |
| `ReasoningOffOptions` | vLLM+OpenAI preset | the JSON merged when the switch is on |
| `ExtraOptions` | *(empty)* | any JSON object merged into the request body |
| `SoundsEnabled` | `true` | the three progress sounds |

Reasoning models (`delta.reasoning` in the stream) answer the same prompt
3× slower because they first emit a private chain of thought we discard.
The default `ReasoningOffOptions` sends both spellings that servers accept:

```ini
ReasoningOffOptions={"chat_template_kwargs":{"enable_thinking":false},"reasoning_effort":"none"}
```

Measured against the live vLLM endpoint: 0.71 s → 0.21 s and 37 discarded
reasoning events → 0. Set `DisableReasoning=false` for a server that rejects
those fields (Traduku also retries once without them on HTTP 400/422, so a
strict gateway costs one extra round-trip, not the translation). `--backend-info`
prints which mode you are in; a malformed `ExtraOptions` fails loudly rather
than silently translating with reasoning on.

Environment knobs, all reported back by `--backend-info` when they change a
decision:

| variable | values | effect |
|---|---|---|
| `TRADUKU_CLIPBOARD` | `wl` · `x11` · `qt` | force a clipboard reader; refused with a reason where it cannot work |
| `TRADUKU_CONFIG` | path | configuration file to use |

## Manual end-to-end check (no real LLM needed)

```bash
./tools/mock-openai.py 8931 &          # streaming OpenAI-compatible mock
traduku --selftest --live    # after pointing config.ini at :8931
traduku --translate --text "Hello"
```

## Troubleshooting

- **`Host xxx not found` although `resolvectl query xxx` works**: glibc never
  reaches DNS for `.local` names when nsswitch reads
  `mdns4_minimal [NOTFOUND=return] dns` - Avahi answers NOTFOUND and the
  lookup stops. Add the host to `/etc/hosts` or use its IP.
- **Translation feels slow**: check `reasoning:` in `traduku --backend-info`;
  a reasoning model streaming a hidden channel is the usual cause (see
  Configuration).
- **"Clipboard is empty" on Wayland**: install `wl-clipboard`. If you suspect
  the reader rather than the clipboard, `TRADUKU_CLIPBOARD=x11 traduku
  --backend-info` prints either `clipboard: x11` or the reason X refused.
- **Double Ctrl+C does nothing on Wayland**: `journalctl --user | grep
  traduku` should show the gesture line the app logs at startup, e.g.
  `traduku: X RECORD key listener active on :0 (helper pid 4231)` or
  `gesture: evdev /dev/input/event*` from `--backend-info`. With no XWayland
  and no `/dev/input` permission the global-shortcut fallback engages
  automatically and its shortcut appears in the tooltip. For the evdev path,
  check the ACL: `getfacl /dev/input/eventN` must list `user:<you>:rw-`.
- **No sounds**: a freedesktop sound theme and *some* player (`pw-play`,
  `paplay`, `aplay`) must exist; `--backend-info` prints
  `audio: <player> - <file>` or the reason it has none, and says nothing
  alarming if you switched sounds off. Sounds are best-effort.
- **Non-Latin layouts**: supported (keycode-based matching); a `ru_RU.UTF-8`
  session also defaults the destination language to Russian.
- **kf.i18n warnings**: the app ships plain-English source strings and no
  catalogs; translations can be added per module later.
