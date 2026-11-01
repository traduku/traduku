#!/usr/bin/env bash
# Build the distro-native package from the working tree and install it in a
# grid VM. Arch: PKGBUILD + makepkg + pacman -U. Debian/Ubuntu: debhelper +
# dpkg-buildpackage + apt install. Ends with the offscreen selftest gate.
# usage: vm-install.sh <vm> [--shell]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
VM=$1
VER=$(sed -n 's/^project(traduku VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")
PVER=$(sed -n 's/^pkgver=\(.*\)$/\1/p' "$ROOT/packaging/arch/PKGBUILD")
SSH="ssh -i $HOME/vms/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
IDX=$("$HERE/vmctl.sh" status 2>/dev/null | awk -v v="$VM" '$1==v{print $3}' | tr -d 'idx=')
SSH="$SSH -p 220${IDX: -1}"
Q() { $SSH vm@127.0.0.1 "$@"; }

case "$VM" in arch-*) DISTRO=arch;; *) DISTRO=debian;; esac

STAGE=$(mktemp -d); TOP=$(mktemp -d); trap 'rm -rf "$STAGE" "$TOP"' EXIT
mkdir -p "$TOP/traduku-$PVER"
cp -a "$ROOT"/. "$TOP/traduku-$PVER/"
rm -rf "$TOP/traduku-$PVER/build" "$TOP/traduku-$PVER/release-out"
tar -czf "$STAGE/tree.tar.gz" -C "$TOP" "traduku-$PVER"
echo "== sending traduku-$PVER tree to $VM"
Q "mkdir -p build && rm -rf build/*"
cat "$STAGE/tree.tar.gz" | Q "tar -xz -C build"

if [ "$DISTRO" = arch ]; then
  echo "== building traduku-$PVER package on $VM (makepkg)"
  Q "cd build && cp traduku-$PVER/packaging/arch/PKGBUILD . && tar -czf traduku-$PVER.tar.gz traduku-$PVER"
  Q "cd build && echo vm | sudo -S pacman -Syu --noconfirm 2>&1 | tail -2 || true"
  Q "cd build && echo vm | sudo -S pacman -S --noconfirm --needed base-devel cmake ninja qt6-base qt6-multimedia qt6-5compat qt6-tools extra-cmake-modules kstatusnotifieritem knotifications kglobalaccel kdbusaddons libx11 libxi libxtst wl-clipboard 2>&1 | tail -2"
  Q "cd build && makepkg -sf --noconfirm --needed --skipchecksums 2>&1 | tail -15"
  echo "== installing with pacman -U"
  Q "cd build && echo vm | sudo -S pacman -U --noconfirm traduku-$PVER-*.pkg.tar.zst 2>&1 | tail -4"
else
  echo "== building .deb on $VM (dpkg-buildpackage, a few minutes)"
  Q "cd build/traduku-$PVER && cp -r packaging/debian debian && echo vm | sudo -S dpkg-buildpackage -us -uc -b 2>&1 | tail -20"
  echo "== installing with apt"
  Q "cd build && echo vm | sudo -S DEBIAN_FRONTEND=noninteractive apt-get install -y --reinstall ./traduku_${PVER}-1_amd64.deb 2>&1 | tail -4"
  Q "set -e; rm -rf /tmp/debx && mkdir /tmp/debx && dpkg-deb -x build/traduku_${PVER}-1_amd64.deb /tmp/debx; a=\$(md5sum /tmp/debx/usr/bin/traduku | cut -d' ' -f1); b=\$(md5sum /usr/bin/traduku | cut -d' ' -f1); echo \"installed md5: \$b (deb \$a)\"; [ \"\$a\" = \"\$b\" ]"
fi

echo "== gate: selftest on $VM"
Q "QT_QPA_PLATFORM=offscreen traduku --selftest 2>&1 | tail -4"
Q "which traduku && traduku --version 2>/dev/null | head -1"
[ "${2:-}" = --shell ] && Q -t bash
exit 0
