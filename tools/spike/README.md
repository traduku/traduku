# tools/spike — Stage 0 diagnostics

Manual probes, not product code and not ctest suites (ROADMAP.md §3,
Stage 0). They answer "which backend can actually work on THIS compositor"
before a line of product code is written, and their output belongs in
ROADMAP.md. Nothing here is installed or linked into `traduku`.

| tool | question it answers | measured on |
|---|---|---|
| `clip-spike probe [--xcb]` | which path reads the DE clipboard from a background client: Qt-wayland (A/B), `wl-paste` (C), raw `XConvertSelection` (E), Qt on XWayland (D) | GNOME 50.1 / Ubuntu 26.04: A,B **fail**; C 13 ms, E 10 ms, D ok |
| `clip-spike seed "text"` | which path can WRITE the clipboard from the background | `QClipboard` on wayland: not owned; `wl-copy` ok; Qt on xcb ok |
| `evdev-spike [list\|listen N]` | can the literal Ctrl+C+C be observed via `/dev/input`, and at what cost (group `input`) | 24 devices, all `EACCES` until the group is granted |
| `portal-spike.py check` | does `GlobalShortcuts` accept a non-sandboxed caller; identity ladder: bus name + `org.freedesktop.Application`, `app_id` in options, installed `.desktop`, launched as a real app | all five: `portal.Error.NotAllowed: An app id is required` |
| `tray-spike [info\|qtsys\|sni\|both]` | which tray protocol can paint an icon *and a menu* here: Qt's `QSystemTrayIcon` (which on Wayland is Qt's generic unix SNI client, not XEmbed) vs. our own `StatusNotifierItem` + `com.canonical.dbusmenu` | GNOME 50.1 / Ubuntu 26.04: both register, both visible; only the generated-from-spec menu implementation gets its calls matched |
| `stage0.sh` | run the whole field test in the graphical session | log: `/tmp/traduku-stage0.log` |

Build (they are on by default, `-DLT_BUILD_SPIKES=OFF` to skip):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/bin/clip-spike --help
```

Two rules for these files: they must not link product code (a broken spike
must still be able to describe reality), and their output is a measurement,
so it is copied into ROADMAP.md instead of being asserted in a test.
