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
| `wl-clipboard` | **Depends** in `debian/`, `optdepends` in `arch/` | Wayland offers the selection only to the focused client, and Traduku is a background tray app. Measured on GNOME 50 (`tools/spike/clip-spike`, ROADMAP.md §2): `QClipboard::text()` returns nothing for a background wayland client, `wl-paste`/`wl-copy` read and write it in ~13 ms. Plasma normally does not need it (the `org.kde.KClipboard` D-Bus is tried first), which is why Arch keeps it optional and the .deb - which builds the GNOME shell - requires it. The app also says it out loud: `IClipboard::diagnostic()` turns "clipboard is empty" into "install wl-clipboard" (`tst_clipboards`, `tst_appcontroller`). |
| `qt6-wayland`, `gstreamer1.0-plugins-good`, `libqt6multimedia6` | Depends (`debian/`) | wayland platform plugin; audio cues for start/finish/error |
| `gnome-shell-extension-appindicator` \| `gnome-shell-ubuntu-extensions` | Recommends (`debian/`) | GNOME has no SNI host without it; the app runs tray-less, so it is not a hard dep |
| `xdg-desktop-portal-gnome` | not packaged yet | the GlobalShortcuts portal is advertised here but rejects non-sandboxed callers (ROADMAP.md §2) - the gesture uses `/dev/input` + gsettings instead |

## Keyboard observation (`/dev/input`) and the udev rule

The literal Ctrl+C+C gesture on Wayland is observed in the kernel input
layer, which is `root:input 0660` by default. Do **not** add users to the
`input` group for it: that lets every program of the account read every
keyboard forever (the Arch community's wording — "a sledgehammer to insert
a pushpin"). The modern answer is a udev `uaccess` rule, which puts an ACL
on the node for the user of the **active seat session only**: locked
screen, another seat, another user — no access.

`udev/70-traduku-uaccess.rules` is that rule. The package does NOT install
it into `/etc` by itself: observing the keyboard is a privacy decision the
user makes per machine.

```bash
sudo install -Dm644 packaging/udev/70-traduku-uaccess.rules \
     /etc/udev/rules.d/70-traduku-uaccess.rules
sudo rm -f /etc/udev/rules.d/99-traduku-uaccess.rules      # earlier name, see below
sudo udevadm control --reload
sudo udevadm trigger --action=add --subsystem-match=input   # add, NOT change
getfacl /dev/input/event3                                   # expect user:<you>:rw-
```

Two traps cost an afternoon each; both were hit on the Ubuntu 26.04 box
this rule was developed on, and both leave `getfacl` unchanged while
everything *looks* installed:

1. **The file name is part of the rule.** Conditions are evaluated against
   the tags collected so far, and the ACL is queued by
   `73-seat-late.rules: TAG=="uaccess", RUN{builtin}+="uaccess"` — so a
   rule called `99-*` sets the tag one step too late: no `:seat:` tag, no
   queued builtin, no ACL, yet `udevadm info` still reports `:uaccess:`.
   It must sort after `60-input-id.rules` (source of `ID_INPUT_KEYBOARD`)
   and before `71-seat.rules`/`73-seat-late.rules` — hence `70-`, the same
   slot the distro's own joystick-only `70-uaccess.rules` occupies. The
   rule also carries `TAG+="seat", RUN{builtin}+="uaccess"` explicitly so
   it keeps working under any name.
2. **`udevadm trigger` defaults to `--action=change`, and `RUN` only
   executes on add/move.** Without `--action=add` nothing is applied.
   Re-plugging the keyboard or rebooting does the same thing.

How the ACL is finally produced (`src/udev/udev-builtin-uaccess.c`, v259):
`device_get_seat()` → `sd_seat_get_active(seat, …, &uid)` → `devnode_acl(fd, uid)`.
Verify a rule without root before installing it:
`udevadm test --extra-rules-dir=<dir> "$(udevadm info -q path -n /dev/input/event3)"`.

(The package does this itself - `traduku-gnome.postinst` on Debian, the
`install=traduku-gnome.install` hook on Arch - and `--backend-info` reports
the state plus the exact fix command; ROADMAP.md §2 records why the file
alone is not enough.)
