# Packaging

Source code is platform-neutral (core/interfaces/ui/app + platform/
layers); this tree holds only distro-specific recipes:

- `arch/`  - PKGBUILD + `mkrelease.sh` (tarball + makepkg, CachyOS/Arch)
- `debian/`- debhelper recipe (Debian/Ubuntu; build with `dpkg-buildpackage`
  on a Debian machine, the recipes are standard 3.0(quilt))
- `../install.sh` - developer install into ~/.local, no root (any distro)

Versioning: CMakeLists `project(... VERSION x.y.z)` is the single source of
truth; PKGBUILD `pkgver` and `debian/changelog` must match on release.
