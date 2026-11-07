#!/usr/bin/env bash
# Prepare a grid VM with the app installed the DISTRO-STANDARD way.
# The package is built on the HOST (native toolchain for Arch; a debian
# container builds the .deb) and pushed to the VM — the VM never compiles.
# usage: vm-install.sh <vm> [--shell] [--rebuild]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
VM=$1
REBUILD=${2:-}
VER=$(sed -n 's/^pkgver=\(.*\)$/\1/p' "$ROOT/packaging/arch/PKGBUILD")

# Which flavour this guest must run. The two shells are different packages
# (traduku = Plasma/KF6, traduku-gnome = GNOME with no KF6 at all), and
# installing the wrong one is not cosmetic: the GNOME guest then needs KF6 it
# does not have, the Plasma guest loses KF6 notifications. So the flavour
# comes from the profile name - and what actually landed is verified below,
# not trusted, because "which build is in front of me" has already cost this
# project two misleading reports.
case "$VM" in
  *-gnome) FLAVOUR=gnome ;;
  *-kde)   FLAVOUR=kde ;;
  *) echo "unknown vm profile $VM (expected *-kde or *-gnome)" >&2; exit 1 ;;
esac
PKGNAME=traduku
[ "$FLAVOUR" = gnome ] && PKGNAME=traduku-gnome
PKG=$ROOT/release-out/${PKGNAME}-${VER}-1-x86_64.pkg.tar.zst
SSH="ssh -i $HOME/vms/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
IDX=$("$HERE/vmctl.sh" status 2>/dev/null | awk -v v="$VM" '$1==v{print $3}' | tr -d 'idx=')
SSH="$SSH -p 220${IDX: -1}"
Q() { $SSH vm@127.0.0.1 "$@"; }
SCP() { scp -i "$HOME/vms/id_ed25519" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -P "220${IDX: -1}" "$@"; }

# Assert that the binary in the guest is the flavour the profile promises.
# `ldd` is the witness: the GNOME shell is configured with no KF6 in the
# link line at all, so a KF6-dependent binary on a gnome guest means the wrong
# package was installed, or packaging put the KDE tree into both.
verify_flavour() {
  local kf6
  kf6=$(Q "ldd /usr/bin/traduku 2>/dev/null | grep -c KF6" || true)
  kf6=$(echo "$kf6" | tr -dc "0-9")
  if [ "$FLAVOUR" = gnome ]; then
    [ "${kf6:-0}" = 0 ] || { echo "FAIL: $VM is a GNOME profile but /usr/bin/traduku links KF6 ($kf6 libs)" >&2; exit 1; }
    Q "test -f /usr/lib/udev/rules.d/70-traduku-uaccess.rules" \
      || { echo "FAIL: traduku-gnome shipped no uaccess rule - the /dev/input gesture gets no ACL without it" >&2; exit 1; }
    Q "command -v wl-paste" \
      || { echo "FAIL: traduku-gnome installed with no wl-clipboard (hard Depends: Wayland clipboard has no Qt fallback)" >&2; exit 1; }
  else
    [ "${kf6:-0}" -gt 0 ] || { echo "FAIL: $VM is a Plasma profile but /usr/bin/traduku links no KF6 - that is the GNOME flavour" >&2; exit 1; }
  fi
  echo "== flavour ok: $FLAVOUR"
}

case "$VM" in
  arch-*)
    # build on the host: fresh pkg tarball?
    if [ "$REBUILD" = --rebuild ] || [ ! -f "$PKG" ] || [ -n "$(find src tests CMakeLists.txt -newer "$PKG" 2>/dev/null | head -1)" ]; then
      echo "== host build (makepkg, ctest gate)"
      QT_QPA_PLATFORM=offscreen ctest --test-dir "$ROOT/build" --output-on-failure >/dev/null 2>&1 || {
        cmake -B "$ROOT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build "$ROOT/build" >/dev/null
        QT_QPA_PLATFORM=offscreen ctest --test-dir "$ROOT/build" --output-on-failure
      }
      "$ROOT/packaging/arch/mkrelease.sh" "$ROOT/release-out" --nocheck
    fi
    echo "== push + pacman -U on $VM"
    # QEMU user-net needs a resolver (baked images predate the resolved patch)
    Q "grep -q DNS /etc/systemd/network/99-dhcp.network 2>/dev/null || echo vm | sudo -S sh -c '
        printf "[Match]\nName=eno* enp* eth*\n[Network]\nDHCP=yes\nDNS=10.0.2.3\n" > /etc/systemd/network/99-dhcp.network
        systemctl enable systemd-resolved; systemctl restart systemd-networkd systemd-resolved' || true"
    # trust the dev-channel signing key (once); real releases are signed
    SCP "$ROOT/packaging/arch/traduku-dev-pub.asc" vm@127.0.0.1:~/
    SCP "$HERE/vm-trust-key.sh" vm@127.0.0.1:~/
    Q "bash ~/vm-trust-key.sh"
    Q "rm -f ~/traduku-*.pkg.tar.zst*"
    # push the release artifacts exactly as a user would download them
    for art in "$PKG" "$PKG.sig"; do
      [ -f "$art" ] && SCP "$art" vm@127.0.0.1:~/
    done
    # rc was once swallowed by '| tail -3': a lock-blocked pacman left the
    # VM uninstalled and the suite ran 8-red on a phantom product.
    Q "echo vm | sudo -S pacman -U --noconfirm ~/${PKGNAME}-${VER}-1-x86_64.pkg.tar.zst >/tmp/pacman-install.log 2>&1 || true; tail -3 /tmp/pacman-install.log; pacman -Q ${PKGNAME}-${VER}"
    verify_flavour
    ;;
  *-kde|*-gnome)
    # .deb built inside the traduku-deb-builder container (dpkg toolchain
    # + Qt6/KF6 deps baked by tools/vms/deb-builder/Dockerfile). Baked
    # ONCE: an in-run apt-get hung on a trixie mirror stall once, and
    # rebuilds must not depend on the network. timeout is the last-resort
    # ceiling for any future container-side wedge.
    echo "== building .deb in the traduku-deb-builder container"
    IMG=traduku-deb-builder:trixie
    docker image inspect "$IMG" >/dev/null 2>&1 || \
      docker build -t "$IMG" "$HERE/deb-builder"
    STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
    timeout 1200 docker run --rm -v "$ROOT:/src:ro" -v "$STAGE:/out" "$IMG" bash -c '
      set -e
      cp -a /src /work; cd /work
      cp -r packaging/debian debian
      # nocheck: debhelper cmake would configure -DBUILD_TESTING=OFF and
      # run nothing; the suite therefore runs as the explicit gate below
      # (old-Qt corner of the matrix: Qt 6.8.2 of debian 13). No exclusions,
      # no repeats - the two old-Qt casualties are fixed in-tree: header-
      # CASE asserts (neighbor 7f82696) and the chunkedtranslator commit-
      # order race (neighbor 7d37663). A red here is a real regression.
      # NOTE: this block lives inside bash -c single quotes: no apostrophes.
      DEB_BUILD_OPTIONS=nocheck dpkg-buildpackage -us -uc -b >/dev/null 2>&1 \
        || DEB_BUILD_OPTIONS=nocheck dpkg-buildpackage -us -uc -b
      # The suite is the gate, run here where the toolchain lives - the
      # guest only ever installs. Both shells: the GNOME one has tests the
      # KDE tree never compiles (evdev gesture, FdoNotifier, single instance).
      for shell in kde gnome; do
        cmake -B gate-$shell -G Ninja -DCMAKE_BUILD_TYPE=Release -DLT_SHELL=$shell >/dev/null
        cmake --build gate-$shell >/dev/null
        ( cd gate-$shell && QT_QPA_PLATFORM=offscreen ctest --output-on-failure ) \
          || { echo "GATE RED: $shell suite" >&2; exit 1; }
      done
      cp /traduku*.deb /out/'
    if [ "$FLAVOUR" = gnome ]; then
      DEB=$(ls "$STAGE"/traduku-gnome_*.deb | head -1)
    else
      # the underscore is load-bearing: traduku*.deb also matches the
      # traduku-gnome artefact and would install the other shell.
      DEB=$(ls "$STAGE"/traduku_*.deb | head -1)
    fi
    [ -f "$DEB" ] || { echo "FAIL: no $FLAVOUR .deb produced (got: $(ls "$STAGE"))" >&2; exit 1; }
    echo "== push + apt install on $VM ($DEB)"
    Q "rm -f ~/traduku*.deb"
    SCP "$DEB" vm@127.0.0.1:~/
    # unattended-upgrades grabs the dpkg lock right after test_03's reboot;
    # an install hitting it used to die silently behind '| tail -3' (the
    # all-red suite after rebuild). Stop the timer, wait for the lock, and
    # VERIFY the package state afterwards - install is a gate, not a hint.
    Q "echo vm | sudo -S sh -c 'systemctl stop unattended-upgrades apt-daily.service apt-daily-upgrade.timer apt-daily-upgrade.service 2>/dev/null || true; i=0; while fuser /var/lib/dpkg/lock-frontend >/dev/null 2>&1 && [ \$i -lt 60 ]; do sleep 2; i=\$((i+1)); done; DEBIAN_FRONTEND=noninteractive apt-get install -y --reinstall /home/vm/${PKGNAME}_*.deb >/tmp/apt-install.log 2>&1' ; tail -3 /tmp/apt-install.log; grep -q '^Setting up ${PKGNAME}' /tmp/apt-install.log && dpkg -l ${PKGNAME} | grep -aq '^ii.*${PKGNAME}'"
    verify_flavour
    ;;
esac

echo "== gate: selftest on $VM"
Q "QT_QPA_PLATFORM=offscreen traduku --selftest 2>&1 | tail -3"
Q "QT_QPA_PLATFORM=offscreen traduku --help 2>&1 | head -3"
[ "$REBUILD" = --shell ] && $SSH -t vm@127.0.0.1 bash
exit 0
