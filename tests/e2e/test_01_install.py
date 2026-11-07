"""E2E-1: install with the distribution's standard package manager.

The user flow under test: a release package sits in the home directory; the
user installs it from a terminal - `sudo pacman -U` on arch (detached .sig,
dev-channel key in the keyring), `sudo apt install ./traduku_*.deb` on
debian/ubuntu. Then the SAME assertions for every distro: registered in the
local package DB, the binary is on PATH, --help answers, the desktop entry
ships, and `traduku --selftest` passes.

Packages are built on the dev host (makepkg / dpkg-buildpackage in the
debian:13 container) - see tools/vms/vm-install.sh; the VM never compiles.

Red checks (verified during development):
- arch: uninstalling first makes version/selftest fail; removing the .sig
  makes pacman refuse ('missing required signature').
- debian: removing the .deb from ~ fails the artifact assert; dpkg -r
  traduku between install and the shared asserts fails the binary assert.
"""
import re

VER = "0.2"


def _common_install_assertions(vm):
    # app binary + desktop entry shipped by the package.
    # (NOTE for the maintainer: --version works only offscreen - it needs
    # QApplication::setApplicationVersion, currently the string is "traduku ".)
    rc, _ = vm.run("command -v traduku")
    assert rc == 0, "binary not on PATH after install"
    rc, out = vm.run("QT_QPA_PLATFORM=offscreen traduku --help 2>&1 | head -3")
    assert "traduku" in out.lower(), f"--help: rc={rc} out={out!r}"
    rc, _ = vm.run("ls /usr/share/applications/traduku.desktop")
    assert rc == 0, "desktop entry missing from the package"

    rc, out = vm.run("QT_QPA_PLATFORM=offscreen traduku --selftest")
    assert rc == 0 and "FAIL" not in out.upper(), f"selftest: {out}"


def _install_arch(vm):
    # release artifacts of this run, as a user would have them in ~
    rc, _ = vm.run(f"ls ~/traduku-{VER}-1-x86_64.pkg.tar.zst ~/traduku-{VER}-1-x86_64.pkg.tar.zst.sig")
    assert rc == 0, "release package/sig not pushed to the VM (run vm-install.sh)"

    # clean slate: the app must not be installed before the test
    vm.sudo("pacman -Rns --noconfirm traduku 2>&1 | tail -1")
    rc, _ = vm.run("command -v traduku")
    assert rc != 0, "precondition: traduku must be absent before the test"

    # the terminal install: `sudo pacman -U` on the signed release package
    # (the detached .sig sits next to it; the dev-channel key is in the VM
    # keyring - the same trust a user adds for a third-party channel).
    rc, out = vm.sudo(
        f"pacman -U --noconfirm /home/vm/traduku-{VER}-1-x86_64.pkg.tar.zst 2>&1 | tail -8")
    assert "installing traduku" in out and "missing required signature" not in out, f"install output: {out}"
    rc, out = vm.run("pacman -Qs '^traduku'")
    assert rc == 0 and "local/traduku" in out, f"not in the local DB: {out}"


def _install_deb(vm):
    # the .deb vm-install.sh built in the debian:13 container, in ~ as a
    # user would have it after downloading the release
    rc, _ = vm.run("ls ~/traduku_*_amd64.deb")
    assert rc == 0, "release .deb not pushed to the VM (run vm-install.sh)"

    # clean slate
    vm.sudo("apt-get remove --purge -y traduku 2>&1 | tail -1")
    rc, _ = vm.run("command -v traduku")
    assert rc != 0, "precondition: traduku must be absent before the test"

    # the terminal install: `sudo apt install ./traduku_*.deb` - apt
    # resolves Depends (libqt6multimedia6, qt6-wayland, gstreamer plugins)
    # from the archive, exactly like a user's double-click on the package.
    rc, out = vm.sudo(
        "DEBIAN_FRONTEND=noninteractive apt-get install -y "
        "/home/vm/$(ls ~/traduku_*_amd64.deb | head -1 | xargs basename) 2>&1 | tail -6")
    assert "Setting up traduku" in out or "traduku (" in out, f"install output: {out}"
    rc, out = vm.run("dpkg -l traduku 2>/dev/null | grep -a '^ii'")
    assert rc == 0 and "traduku" in out, f"not installed in the dpkg DB: {out}"


def test_install_from_release_package_with_distro_manager(session):
    vm = session
    if "arch" in vm.vm:
        _install_arch(vm)
    else:
        _install_deb(vm)
    _common_install_assertions(vm)
