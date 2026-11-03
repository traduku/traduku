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
PKG=$ROOT/release-out/traduku-${VER}-1-x86_64.pkg.tar.zst
SSH="ssh -i $HOME/vms/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
IDX=$("$HERE/vmctl.sh" status 2>/dev/null | awk -v v="$VM" '$1==v{print $3}' | tr -d 'idx=')
SSH="$SSH -p 220${IDX: -1}"
Q() { $SSH vm@127.0.0.1 "$@"; }
SCP() { scp -i "$HOME/vms/id_ed25519" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -P "220${IDX: -1}" "$@"; }

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
    Q "echo vm | sudo -S pacman -U --noconfirm ~/traduku-${VER}-1-x86_64.pkg.tar.zst >/tmp/pacman-install.log 2>&1 || true; tail -3 /tmp/pacman-install.log; pacman -Q traduku-${VER}"
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
      cmake -B gatebuild -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
      cmake --build gatebuild >/dev/null
      cd gatebuild
      QT_QPA_PLATFORM=offscreen ctest --output-on-failure
      cp /traduku_*.deb /out/'
    DEB=$(ls "$STAGE"/traduku_*.deb | head -1)
    echo "== push + apt install on $VM"
    Q "rm -f ~/traduku_*.deb"
    SCP "$DEB" vm@127.0.0.1:~/
    # unattended-upgrades grabs the dpkg lock right after test_03's reboot;
    # an install hitting it used to die silently behind '| tail -3' (the
    # all-red suite after rebuild). Stop the timer, wait for the lock, and
    # VERIFY the package state afterwards - install is a gate, not a hint.
    Q "echo vm | sudo -S sh -c 'systemctl stop unattended-upgrades apt-daily.service apt-daily-upgrade.timer apt-daily-upgrade.service 2>/dev/null || true; i=0; while fuser /var/lib/dpkg/lock-frontend >/dev/null 2>&1 && [ \$i -lt 60 ]; do sleep 2; i=\$((i+1)); done; DEBIAN_FRONTEND=noninteractive apt-get install -y --reinstall /home/vm/traduku_*.deb >/tmp/apt-install.log 2>&1' ; tail -3 /tmp/apt-install.log; grep -q '^Setting up traduku' /tmp/apt-install.log && dpkg -l traduku | grep -aq '^ii.*traduku'"
    ;;
esac

echo "== gate: selftest on $VM"
Q "QT_QPA_PLATFORM=offscreen traduku --selftest 2>&1 | tail -3"
Q "QT_QPA_PLATFORM=offscreen traduku --help 2>&1 | head -3"
[ "$REBUILD" = --shell ] && $SSH -t vm@127.0.0.1 bash
exit 0
