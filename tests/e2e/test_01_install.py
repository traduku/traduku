"""E2E-1: install with the distribution's standard package manager.

The user flow under test: a release package sits in the home directory; the
user installs it from a terminal - `sudo pacman -U` on arch (detached .sig,
dev-channel key in the keyring), `sudo apt install ./traduku*.deb` on
debian/ubuntu. Then the SAME assertions for every distro: registered in the
local package DB, the binary is on PATH, --help answers, the desktop entry
ships, and `traduku --selftest` passes.

The guest never compiles. Everything below is about the *package*: which
flavour a desktop gets, and whether the files that flavour owes are inside it.
That is the part a source-tree build cannot test - a missing Depends, a
missing data file, or the wrong shell in both packages only exist once dpkg
and pacman have had their say.

Flavours (see packaging/debian/control): `traduku` is the Plasma shell and
links KF6; `traduku-gnome` is the GNOME shell and must link none of it, must
own the udev uaccess rule the /dev/input gesture needs, and must have pulled
wl-clipboard as a hard dependency.

Packages are built on the dev host (makepkg / dpkg-buildpackage in the
traduku-deb-builder container) - see tools/vms/vm-install.sh.

Red checks (verified during development):
- arch: uninstalling first makes version/selftest fail; removing the .sig
  makes pacman refuse ('missing required signature').
- debian: removing the .deb from ~ fails the artifact assert; dpkg -r
  traduku between install and the shared asserts fails the binary assert.
- installing the Plasma flavour on a gnome profile fails the KF6 assert, and
  deleting the udev rule from the tree fails the GNOME file assert.
"""
import re
from pathlib import Path

# One version, three places that must agree (CMakeLists, PKGBUILD, changelog)
# - tests/cmake/test_versions_agree.sh fails the build when they drift, so
# taking it from PKGBUILD here is reading the same truth the package used,
# not duplicating a literal that was already wrong once (it said "0.2" while
# the build said 0.2.0).
_PKGVER = re.search(
    r"^pkgver=(\S+)$",
    (Path(__file__).resolve().parents[2] / "packaging" / "arch" / "PKGBUILD")
    .read_text(encoding="utf-8"),
    re.M,
).group(1)


def _flavour(vm):
    """The package this desktop must run, derived from the profile."""
    return "traduku-gnome" if vm.vm.endswith("-gnome") else "traduku"


def _common_install_assertions(vm):
    # app binary + desktop entry shipped by the package.
    rc, _ = vm.run("command -v traduku")
    assert rc == 0, "binary not on PATH after install"
    rc, out = vm.run("QT_QPA_PLATFORM=offscreen traduku --help 2>&1 | head -3")
    assert "traduku" in out.lower(), f"--help: rc={rc} out={out!r}"
    rc, _ = vm.run("ls /usr/share/applications/traduku.desktop")
    assert rc == 0, "desktop entry missing from the package"
    rc, out = vm.run("QT_QPA_PLATFORM=offscreen traduku --version")
    assert _PKGVER in out, f"--version says {out!r}, expected to contain {_PKGVER!r}"

    rc, out = vm.run("QT_QPA_PLATFORM=offscreen traduku --selftest")
    assert rc == 0 and "FAIL" not in out.upper(), f"selftest: {out}"


def _assert_flavour(vm):
    """The package must be the shell the desktop runs - verified, not assumed.

    `ldd` is the witness for the GNOME one: that tree is configured with no KF6
    in the link line at all, so a KF6-dependent binary there means the wrong
    tree went into the package, which is exactly what the .deb did before
    packaging was split (every distro got the Plasma shell).
    """
    rc, out = vm.run("ldd /usr/bin/traduku 2>/dev/null | grep -c KF6")
    kf6 = int(re.search(r"\d+", out).group()) if re.search(r"\d+", out) else 0
    if vm.vm.endswith("-gnome"):
        assert kf6 == 0, f"{vm.vm} is a GNOME profile but the binary links {kf6} KF6 libs"
        rc, _ = vm.run("test -f /usr/lib/udev/rules.d/70-traduku-uaccess.rules")
        assert rc == 0, ("traduku-gnome shipped no uaccess rule; without it the "
                         "/dev/input gesture gets no ACL and Ctrl+C+C is dead")
        rc, _ = vm.run("command -v wl-paste")
        assert rc == 0, ("wl-clipboard is a hard Depends of traduku-gnome (Qt's "
                         "clipboard cannot be read from a background process on Wayland)")
        rc, out = vm.run("QT_QPA_PLATFORM=offscreen traduku --backend-info 2>&1")
        assert "shell: gnome" in out, f"--backend-info on a gnome guest: {out}"
    else:
        assert kf6 > 0, (f"{vm.vm} is a Plasma profile but the binary links no KF6 - "
                         "that is the GNOME flavour")
        rc, _ = vm.run("ls /usr/share/knotifications6/traduku.notifyrc")
        assert rc == 0, "the Plasma flavour must ship the notification catalogue"
        rc, out = vm.run("QT_QPA_PLATFORM=offscreen traduku --backend-info 2>&1")
        assert "shell: kde" in out, f"--backend-info on a kde guest: {out}"


def test_backend_info_survives_a_plain_ssh_session(vm):
    """`traduku --backend-info` is what a user types when the desktop broke.

    That means: over ssh, with no DISPLAY, no WAYLAND_DISPLAY and no
    QT_QPA_PLATFORM. Measured before this test existed, that exact call
    aborted - Qt's platform selection qFatals with "no platform plugin could
    be initialized" and the process dies printing nothing at all, because the
    report is buffered and abort() never flushes it. The other assertions in
    this file pass QT_QPA_PLATFORM=offscreen, which hides precisely that.
    """
    rc, out = vm.run("traduku --backend-info 2>&1; echo \"rc=$?\"")
    assert "rc=0" in out, (
        f"--backend-info died on a headless shell (rc line missing, exit {rc}): "
        f"a support command that needs a desktop is no support command.\n{out}")
    assert "shell:" in out, f"--backend-info answered without saying which shell: {out}"
    assert "clipboard:" in out, (
        f"--backend-info named every backend but the one users call broken: {out}")


def test_clipboard_backend_is_the_one_the_session_needs(vm, session):
    """Which clipboard reader the app picks, asked from inside the session.

    On Arch there is no wl-clipboard at all (AUR only), so the choice is
    X11-or-Qt, and Qt is not neutral there: its xcb backend reads a plain
    TEXT offer as latin-1 and hands back mojibake for Cyrillic. On GNOME the
    package hard-depends on wl-clipboard, so picking anything else means the
    dependency is decorative.
    """
    rc, out = vm.ses("traduku --backend-info 2>&1")
    lines = [l for l in out.splitlines() if l.startswith("clipboard:")]
    assert lines, f"no clipboard line from --backend-info (rc={rc}):\n{out}"
    got = lines[0].split(":", 1)[1].strip()
    if vm.vm.endswith("-gnome"):
        assert got == "wl-clipboard", (
            f"{vm.vm}: wl-clipboard is a hard Depends, so the app should read the "
            f"compositor's clipboard through it, not through {got!r}")
    elif vm.vm.startswith("arch"):
        assert got == "x11", (
            f"{vm.vm}: no wl-clipboard in the repos, so the wayland clipboard is "
            f"reachable through XWayland only - {got!r} is the reader that garbles "
            f"a plain TEXT offer")


def _install_arch(vm):
    pkg = _flavour(vm)
    art = f"{pkg}-{_PKGVER}-1-x86_64.pkg.tar.zst"
    # release artifacts of this run, as a user would have them in ~
    rc, _ = vm.run(f"ls ~/{art} ~/{art}.sig")
    assert rc == 0, f"release package/sig {art} not pushed to the VM (run vm-install.sh)"

    # clean slate: neither flavour may be installed before the test
    for other in ("traduku", "traduku-gnome"):
        vm.sudo(f"pacman -Rns --noconfirm {other} 2>&1 | tail -1")
    rc, _ = vm.run("command -v traduku")
    assert rc != 0, "precondition: traduku must be absent before the test"

    # the terminal install: `sudo pacman -U` on the signed release package
    # (the detached .sig sits next to it; the dev-channel key is in the VM
    # keyring - the same trust a user adds for a third-party channel).
    rc, out = vm.sudo(f"pacman -U --noconfirm /home/vm/{art} 2>&1 | tail -8")
    assert f"installing {pkg}" in out, f"install output: {out}"
    assert "missing required signature" not in out, f"unsigned package accepted: {out}"
    rc, out = vm.run(f"pacman -Qs '^{pkg}$'")
    assert rc == 0 and f"local/{pkg}" in out, f"not in the local DB: {out}"


def _install_deb(vm):
    pkg = _flavour(vm)
    # the .deb vm-install.sh built in the traduku-deb-builder container, in ~
    # as a user would have it after downloading the release. The underscore
    # after the name matters: `traduku*` also matches traduku-gnome_*.
    rc, _ = vm.run(f"ls ~/{pkg}_*_amd64.deb")
    assert rc == 0, f"release {pkg}_*.deb not pushed to the VM (run vm-install.sh)"

    # clean slate - both flavours, they Conflict with each other and dpkg
    # would otherwise refuse the install with a dependency problem
    for other in ("traduku", "traduku-gnome"):
        vm.sudo(f"apt-get remove --purge -y {other} 2>&1 | tail -1")
    rc, _ = vm.run("command -v traduku")
    assert rc != 0, "precondition: traduku must be absent before the test"

    # the terminal install: `sudo apt install ./traduku*.deb` - apt resolves
    # Depends (libqt6multimedia6, qt6-wayland, gstreamer plugins, and
    # wl-clipboard on the GNOME flavour) from the archive, exactly like a
    # user's double-click on the package.
    rc, out = vm.sudo(
        "DEBIAN_FRONTEND=noninteractive apt-get install -y "
        f"/home/vm/$(ls ~/{pkg}_*_amd64.deb | head -1 | xargs basename) 2>&1 | tail -8")
    # apt's prose is not the contract: the tail of its output is needrestart
    # chatter, and re-installing the version already on disk prints almost
    # nothing. dpkg's own state is the thing a user's system ends up in.
    rc, out = vm.sudo(f"dpkg-query -W -f='${{Status}} ${{Version}}' {pkg}")
    assert "install ok installed" in out, f"not installed in the dpkg DB: {out}"
    assert _PKGVER in out, f"installed version is not the released one: {out}"


def test_install_from_release_package_with_distro_manager(session):
    vm = session
    if "arch" in vm.vm:
        _install_arch(vm)
    else:
        _install_deb(vm)
    _common_install_assertions(vm)
    _assert_flavour(vm)
