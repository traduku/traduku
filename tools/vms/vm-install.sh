#!/usr/bin/env bash
# Build the .deb from HEAD and install it into a grid VM (TDD gate included).
# usage: vm-install.sh <vm> [--shell]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
VM=$1
VER=$(sed -n 's/^project(traduku VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")
SSH="ssh -i $HOME/vms/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
IDX=$("$HERE/vmctl.sh" status 2>/dev/null | awk -v v="$VM" '$1==v{print $3}' | tr -d 'idx=')
SSH="$SSH -p 220${IDX:-1}"

git -C "$ROOT" stash list >/dev/null
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
# Release-style tarball of the working tree (HEAD + uncommitted packaging fixes)
TMP=$(mktemp -d); TOP=$(mktemp -d); trap 'rm -rf "$STAGE" "$TMP" "$TOP"' EXIT
mkdir -p "$TOP/traduku-$VER"
cp -a "$ROOT"/. "$TOP/traduku-$VER/"
rm -rf "$TOP/traduku-$VER/build" "$TOP/traduku-$VER/release-out"
tar -czf "$STAGE/traduku-$VER.tar.gz" -C "$TOP" "traduku-$VER"
echo "== sending traduku-$VER.tar.gz to $VM"
$SSH vm@127.0.0.1 "mkdir -p build && echo vm | sudo -S rm -rf build/* 2>/dev/null; rm -rf build/*"
$SSH vm@127.0.0.1 "rm -rf build/traduku-$VER" 
cat "$STAGE/traduku-$VER.tar.gz" | $SSH vm@127.0.0.1 "tar -xz -C build"

echo "== building .deb on $VM (dpkg-buildpackage, this takes a few minutes)"
$SSH vm@127.0.0.1 "cd build/traduku-$VER && cp -r packaging/debian debian && echo vm | sudo -S dpkg-buildpackage -us -uc -b 2>&1 | tail -20"

echo "== installing on $VM"
$SSH vm@127.0.0.1 "cd build && echo vm | sudo -S DEBIAN_FRONTEND=noninteractive apt-get install -y ./traduku_${VER}-1_amd64.deb 2>&1 | tail -4"

echo "== gate: selftest on $VM"
$SSH vm@127.0.0.1 "QT_QPA_PLATFORM=offscreen traduku --selftest 2>&1 | tail -4"
$SSH vm@127.0.0.1 "which traduku && dpkg -l traduku | tail -1"
[ "${2:-}" = --shell ] && $SSH -t vm@127.0.0.1 bash
