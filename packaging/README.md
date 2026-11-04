# Packaging

Source code is platform-neutral (core/interfaces/ui/app + platform/
layers); this tree holds only distro-specific recipes:

- `arch/`  - PKGBUILD + `mkrelease.sh` (tarball + makepkg, CachyOS/Arch)
- `debian/`- debhelper recipe (Debian/Ubuntu; build with `dpkg-buildpackage`
  on a Debian machine, the recipes are standard 3.0(quilt))
- `../install.sh` - developer install into ~/.local, no root (any distro)

Versioning: CMakeLists `project(... VERSION x.y.z)` is the single source of
truth; PKGBUILD `pkgver` and `debian/changelog` must match on release.

## Why these runtime dependencies

| package | where | why |
|---|---|---|
| `wl-clipboard` | **Depends** in `debian/`, `optdepends` in `arch/` | Wayland offers the selection only to the focused client, and Traduku is a background tray app. Measured on GNOME 50 (`tools/spike/clip-spike`, GNOME_PLAN.md §0b): `QClipboard::text()` returns nothing for a background wayland client, `wl-paste`/`wl-copy` read and write it in ~13 ms. Plasma normally does not need it (the `org.kde.KClipboard` D-Bus is tried first), which is why Arch keeps it optional and the .deb - which builds the GNOME shell - requires it. The app also says it out loud: `IClipboard::diagnostic()` turns "clipboard is empty" into "install wl-clipboard" (`tst_clipboards`, `tst_appcontroller`). |
| `qt6-wayland`, `gstreamer1.0-plugins-good`, `libqt6multimedia6` | Depends (`debian/`) | wayland platform plugin; audio cues for start/finish/error |
| `gnome-shell-extension-appindicator` \| `gnome-shell-ubuntu-extensions` | Recommends (`debian/`) | GNOME has no SNI host without it; the app runs tray-less, so it is not a hard dep |
| `xdg-desktop-portal-gnome` | not packaged yet | the GlobalShortcuts portal is advertised here but rejects non-sandboxed callers (GNOME_PLAN.md §0b) - the gesture uses `/dev/input` + gsettings instead |

## Keyboard observation (`/dev/input`) and the udev rule

The literal Ctrl+C+C gesture on Wayland is observed in the kernel input
layer, which is root:input 0660 by default. Do **not** add users to the
`input` group for this (it makes every program a keylogger; the Arch
community calls it "a sledgehammer to insert a pushpin", and the modern
answer there is udev `uaccess` + logind).

`udev/99-traduku-uaccess.rules` hands the keyboard nodes to the **active
seat session only** (a logind ACL, the same mechanism that already gives
this user `/dev/video0` - verified on Ubuntu 26.04 with `udevadm test
--extra-rules-dir`). It is deliberately NOT installed by the package:
observing the keyboard is a privacy decision the user makes per machine.

```bash
sudo install -Dm644 packaging/udev/99-traduku-uaccess.rules \
     /etc/udev/rules.d/99-traduku-uaccess.rules
sudo udevadm control --reload && sudo udevadm trigger --subsystem-match=input
```

(Stage 3 of GNOME_PLAN.md turns this into `traduku --grant-keyboard-access
[--revoke]` and reports the state in `--backend-info`.)
