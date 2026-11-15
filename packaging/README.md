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

## Windows

Two artifacts from one staging tree (`dist/portable`, filled by
`build-portable.sh` via `deploy-runtime.sh` — Qt plugins, the MinGW trio by
name, and the offscreen platform that `--selftest` needs):

| script | what it writes | gate row |
|---|---|---|
| `windows/build-portable.sh` | `traduku-<v>-win64-portable.zip` | `portable_zip` |
| `windows/build-msi.sh` | `traduku-<v>-x64.msi` | `msi`, `msi_e2e` |

The zip is the primary package: no UAC, works for a user without admin rights,
and it is the one 0.4 shipped. The `.msi` is the same files in the form people
expect — one entry in "Installed apps", install and uninstall from Settings.
`windows/product.wxs` records the decisions that come with it: per-machine (a
global-hotkey tray app is one program per machine, the price is UAC, and the
zip is the answer for anyone who will not pay it); the runtime is payload files,
not merge modules (MinGW has no redist to chain); the config in
`%LOCALAPPDATA%\traduku\config.ini` survives uninstall (a person's API key is
not the installer's to delete); autostart belongs to the Settings window, not to
the MSI (two writers of one setting will disagree); the licence dialog shows the
repository's `LICENSE`, converted to RTF at build time, so `LICENSE` stays the
only licence text in the tree.

The file list is **harvested** (`heat dir`), never written by hand — a package
that carries a list requiring an edit when a DLL appears ships yesterday's list,
and the user meets it as "Qt6Svg.dll not found" from a green build. The count is
compared with the staging tree before and after: `harvested components == staged
files`.

`mkzip.sh`/`check-portable-zip.sh` exist because of ROADMAP.md §7.8 item 5 (the
0.4.0 zip that Windows refused to open, verified by the tool that wrote it), and
the MSI follows the same rule: `msi-inspect.ps1` reads the finished package back
**through `msi.dll`** — the engine on the user's machine — and asks about tables:
File vs the tree, the required Qt/MinGW files by name, `platforms\` present,
every component referenced by a feature, the cabinet embedded (`#cab1.cab`; a
`.cab` sibling is a package that installs on the machine that built it), one
UpgradeCode across the Upgrade rows, package type 2 (per-machine), template
`x64;`, the `VersionNT64 >= 603` launch condition, and the `CREATEDESKTOPSHORTCUT`
condition on the shortcut component. candle and light exit 0 for a package that
installs the wrong thing; this does not.

The `msi` gate then builds three deliberate mutants and requires each to be
refused by the check meant to catch it: a cabinet that is not embedded, a
shortcut that cannot be declined, a payload with one plugin missing. A gate that
cannot reproduce the incident measures its own optimism.

Install round-trip (`test-msi-install.ps1`: install → `--selftest` from the
install dir with no Qt on `PATH` → `--translate` against `tools/mock-openai.py`
→ the "Installed apps" entry in HKLM → uninstall → no files left) needs
elevation. `build-msi.sh` runs it when the shell is elevated, asks Windows with
`--prompt-uac` on a desktop, and prints the skip with its reason otherwise — a
red "1603" from a test that never installed anything is worse than a skip. CI
Windows runners run elevated, so the `msi_e2e` row executes there.

The translate stage is what makes it an e2e of the product and not of msiexec:
the program that landed in `%ProgramFiles%` answers a translation request, with
a `PATH` that contains no Qt, so the network stack that replies is the one the
package carried. A package that ships `Qt6Core.dll` and forgets `Qt6Network.dll`
passes `--selftest` and cannot talk to an API. The endpoint is `127.0.0.1` on a
random port and the config is a temp file via `TRADUKU_CONFIG` — the row cannot
reach a real API or spend a real key. The row is followed by two controls, both
required to fail: a package with the cabinet not embedded (the two-file package:
it installs on the machine that built it, fails on the one that downloaded it),
and the translate stage with nothing listening on its port ("the program
translates" must be a thing the script checked, not a line it prints).

Tools: WiX Toolset v3 (`candle`, `light`, `heat`). Either
`winget install --id WiXToolset.WiXToolset` (UAC), or, with no admin rights, the
binaries zip unpacked into the profile — which is what this machine uses:

```bash
curl -sL -o wix.zip https://github.com/wixtoolset/wix3/releases/download/wix3141rtm/wix314-binaries.zip
mkdir -p "$LOCALAPPDATA/wix314" && /c/Windows/System32/tar.exe -xf wix.zip -C "$LOCALAPPDATA/wix314"
packaging/windows/build-msi.sh            # finds it, no install, no UAC
```

One caveat worth knowing before you compare checksums: a WiX package with
`Product/@Id="*"` is **not byte-reproducible** — the ProductCode GUID and the
summary timestamps are regenerated every run, so `traduku-0.4.0-x64.msi` gets a
new sha256 on every build of the same source. What is reproducible, and checked,
is the payload: the `traduku.exe` inside the package is compared with the build
output (`sha256sum`, and the `msi` gate prints it). Publish the `.msi` you
installed, not the one you inspect.
