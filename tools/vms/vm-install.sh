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
    Q "rm -f ~/traduku-*.pkg.tar.zst"
    SCP "$PKG" vm@127.0.0.1:~/
    Q "echo vm | sudo -S sh -c 'sed s/^SigLevel.*/SigLevel\\ =\\ Never/ /etc/pacman.conf > /tmp/pacman-grid.conf && pacman -U --noconfirm --config /tmp/pacman-grid.conf /home/vm/traduku-${VER}-1-x86_64.pkg.tar.zst 2>&1 | tail -3'"
    ;;
  *-kde|*-gnome)
    # .deb built inside the debian:13 container (dpkg toolchain), installed via apt
    echo "== building .deb in the debian:13 container"
    STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
    docker run --rm -v "$ROOT:/src:ro" -v "$STAGE:/out" debian:13 bash -c '
      apt-get update -qq >/dev/null && apt-get install -y -qq --no-install-recommends \
        build-essential cmake ninja-build pkg-config debhelper dpkg-dev \
        qt6-base-dev qt6-multimedia-dev libqt6core5compat6-dev \
        libkf6coreaddons-dev libkf6i18n-dev libkf6statusnotifieritem-dev \
        libkf6notifications-dev libkf6globalaccel-dev libkf6dbusaddons-dev \
        libx11-dev libxi-dev libxtst-dev >/dev/null
      cp -a /src /work; cd /work; cp -r packaging/debian debian
      dpkg-buildpackage -us -uc -b >/dev/null 2>&1 || dpkg-buildpackage -us -uc -b
      cp /traduku_*.deb /out/'
    DEB=$(ls "$STAGE"/traduku_*.deb | head -1)
    echo "== push + apt install on $VM"
    Q "rm -f ~/traduku_*.deb"
    SCP "$DEB" vm@127.0.0.1:~/
    Q "echo vm | sudo -S DEBIAN_FRONTEND=noninteractive apt-get install -y --reinstall ~/traduku_*.deb 2>&1 | tail -3"
    ;;
esac

echo "== gate: selftest on $VM"
Q "QT_QPA_PLATFORM=offscreen traduku --selftest 2>&1 | tail -3"
Q "traduku --version"
[ "$REBUILD" = --shell ] && $SSH -t vm@127.0.0.1 bash
exit 0
