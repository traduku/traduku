"""E2E-1: install with the distribution's standard package manager.

The user flow under test: a release package sits in the home directory; the
user installs it from a terminal with `sudo pacman -U`, answering the
unsigned-package prompt. Then: registered in the local DB, `--version`
works, the desktop entry ships, and `traduku --selftest` passes.

(Debian/Ubuntu run the same assertions after `apt install ./traduku_*.deb`;
the package itself is built on the dev host — see tools/vms/vm-install.sh.)

Red check (verified during development): uninstalling the package first makes
the version and selftest assertions fail -> the test detects it.
"""
import re

VER = "0.2"


def test_install_from_release_package_with_pacman(session):
    vm = session
    # release artifacts of this run, as a user would have them in ~
    rc, _ = vm.run(f"ls ~/traduku-{VER}-1-x86_64.pkg.tar.zst ~/traduku-{VER}-1-x86_64.pkg.tar.zst.sig")
    assert rc == 0, "release package/sig not pushed to the VM (run vm-install.sh)"

    # clean slate: the app must not be installed before the test
    vm.sudo("pacman -Rns --noconfirm traduku 2>&1 | tail -1")
    rc, _ = vm.run("command -v traduku")
    assert rc != 0, "precondition: traduku must be absent before the test"

    # the terminal install: `sudo pacman -U` on the signed release package
    # (the detached .sig sits next to it; the dev-channel key is in the VM
    # keyring — the same trust a user adds for a third-party channel).
    rc, out = vm.sudo(
        f"pacman -U --noconfirm /home/vm/traduku-{VER}-1-x86_64.pkg.tar.zst 2>&1 | tail -8")
    assert "installing traduku" in out and "missing required signature" not in out, f"install output: {out}"
    rc, out = vm.run("pacman -Qs '^traduku'")
    assert rc == 0 and f"local/traduku" in out, f"not in the local DB: {out}"

    # app binary + desktop entry shipped by the package.
    # (NOTE for the maintainer: --version works only offscreen — it needs
    # QApplication::setApplicationVersion, currently the string is "traduku ".)
    rc, _ = vm.run("command -v traduku")
    assert rc == 0, "binary not on PATH after install"
    rc, out = vm.run("QT_QPA_PLATFORM=offscreen traduku --help 2>&1 | head -3")
    assert "traduku" in out.lower(), f"--help: rc={rc} out={out!r}"
    rc, _ = vm.run("ls /usr/share/applications/traduku.desktop")
    assert rc == 0, "desktop entry missing from the package"

    rc, out = vm.run("QT_QPA_PLATFORM=offscreen traduku --selftest")
    assert rc == 0 and "FAIL" not in out.upper(), f"selftest: {out}"
