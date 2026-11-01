## Installation

For end users install the package (tray app + desktop entry + sounds;
the app asks about autostart and opens settings on first launch):

- Arch/CachyOS: `packaging/arch/mkrelease.sh` builds
  `release-out/traduku-0.1-1-x86_64.pkg.tar.zst`; install with
  `pacman -U`.
- Debian/Ubuntu: recipes in `packaging/debian/` (debhelper, cmake+ninja).

For development use `./install.sh` (~/.local, no root).

# Traduku

A modular, streaming clipboard translator for Linux desktops, powered by any
OpenAI-compatible LLM endpoint (vLLM, llama.cpp server, Ollama, LM Studio,
OpenAI, OpenRouter, …). Select text, press **Ctrl+C twice** — a
DeepL-style window opens and the translation **types itself out live** as
the model streams it.

Rewritten from the ground up out of the `kde-translate` prototype: the core
is desktop-agnostic (plain Qt), KDE/Plasma is just one pluggable shell, and
a GNOME shell only needs to implement the small backend interfaces.

## Features

- **Double Ctrl+C** launches translation of the clipboard.
  - Passive on X11 and on Wayland-with-XWayland: XInput2 raw events (RECORD
    fallback) observe keys without grabbing them — normal copying untouched.
  - Layout-independent (matches the physical C key by keycode, works with
    Cyrillic/Greek/… layouts; XKB modifier probe for native-Wayland focus).
  - Fallback: a registered global shortcut (default `Ctrl+Alt+C`, press
    twice) via `KGlobalAccel`; pair-detection reuses the same tested
    detector as the passive path.
- **Streaming result window** (`Original | Translation` side by side):
  source on the left, the LLM's answer appended live on the right, language
  picker, *Copy translation*, *Settings…*, status line. Long texts are no
  problem — the window scrolls, tray tooltips never could.
- **Audio progress cues**: a start chime when the gesture fires, a
  completion sound when the translation is ready, an error sound on failure
  (freedesktop theme; toggleable in Settings). You can follow the process
  without watching the screen.
- **Configurable endpoint** (address / API key / model), destination
  language, double-press window, clipboard behaviour — stored as a plain
  INI in `~/.config/traduku/config.ini`, editable from any DE.
- **Clipboard**: Qt (X11) / `wl-clipboard` (Wayland) selected at runtime.
- **Launch on Login** via XDG autostart (KDE, GNOME, XFCE all honour it).
- One-shot CLI: `traduku --translate [--text "..."]` streams the
  translation to stdout — scripts, pipes, tests.

## Architecture

```
src/core/        pure Qt, no DE: settings (INI), languages, SSE streaming
                 parser, LLM client (stream=true), translation engine,
                 double-press detector, shortcut monitor, sound player
src/interfaces/  IClipboard · IHotkeyBackend · INotifier · IAutostart ·
                 IGlobalShortcut          (the DE-portability seam)
src/platform/qt/ QtClipboard (X11) · XdgAutostart (XDG autostart spec)
src/platform/wl/ WlClipboard (wl-paste/wl-copy) + clipboard factory
src/platform/x11/ X11HotkeyBackend (XI2 raw / RECORD, passive monitor)
src/platform/kde/ KdeTray (KStatusNotifierItem) · KdeNotifier
                 (KNotification) · KdeGlobalShortcut (KGlobalAccel)
src/ui/          TranslatorWindow · SettingsDialog   (Qt Widgets only)
src/app/         AppController (composition root, DE-agnostic) · SelfTest
src/app/kde/     traduku main (wiring, CLI, single instance)
```

`AppController` connects the lifecycle — gesture → clipboard read → engine
streams → window updates → clipboard write / sounds / notifications. A
`GNOME` shell would build the same struct with AppIndicator +
libnotify + GNOME Shortcuts + libei; nothing in `src/core` changes.

## Build & test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure   # 15 unit/integration suites
./install.sh                                 # into ~/.local, incl. desktop data
```

Build deps: Qt 6.4+ (Core Gui Widgets Network Test), CMake ≥ 3.21, a C++17
compiler; KDE layer: KF6 (StatusNotifierItem, Notifications, GlobalAccel,
DBusAddons); X11 layer: Xlib + XInput + XRecord (`x11`, `xtst`, `xi` dev
packages). X11/KDE layers are optional at configure time
(`-DLT_BUILD_X11=OFF`, `-DLT_BUILD_KDE=OFF`).

Every module was written test-first (see ARCHITECTURE.md and the commit history):
unit tests for each core piece, integration tests over an in-process
fragmented mock SSE server, offscreen UI tests, plus
`traduku --selftest [--live]` for users to verify an install.

## Usage

1. `traduku &` → tray icon appears.
2. Tray → *Settings…* → API address (e.g. `http://localhost:11434/v1`),
   model, destination language. *Test API* checks reachability.
3. Select text → press **Ctrl+C twice quickly** → the window opens
   and streams the translation; start/finish sounds mark the boundaries.
   When enabled, the finished translation also replaces the clipboard.
4. Tray: translate now, destination language, autostart, settings, quit.
   Window: pick destination language (persisted), copy, settings.

## Manual end-to-end check (no real LLM needed)

```bash
./tools/mock-openai.py 8931 &          # streaming OpenAI-compatible mock
traduku --selftest --live    # after pointing config.ini at :8931
traduku --translate --text "Hello"
```

## Troubleshooting (inherited from the prototype)

- **Wayland, "Clipboard is empty"**: install `wl-clipboard`.
- **Double Ctrl+C dead on Wayland**: `journalctl --user | grep traduku`
  should show `XI2 raw key listener active on :0`; without XWayland the
  global-shortcut fallback engages automatically (its shortcut appears in
  the tooltip).
- **No sounds**: a freedesktop sound theme + `paplay`/`pw-play` must exist;
  sounds are best-effort and can be switched off in Settings.
- **Non-Latin layouts**: supported (keycode-based matching).
- **kf.i18n warnings**: the app ships plain-English source strings and no
  catalogs; translations can be added per module later.
