#!/usr/bin/env bash
# Produce the release tarball and the Arch package (CachyOS/Arch).
# Output: $1 (default: ./release-out), git-tag the HEAD commit yourself after.
#
# What goes into the tarball is the question this script exists to answer
# honestly. Default is HEAD, and a dirty working tree is refused: a "release"
# that silently contains whatever was in the editor is not a release.
#
#   --allow-dirty   pack the working tree instead (tracked + untracked files
#                   that are not gitignored), and say so loudly. tools/vms/
#                   vm-install.sh uses it: the grid is supposed to test the
#                   code that was just built and ctest-ed. With HEAD here and
#                   the working tree everywhere else, an uncommitted fix got a
#                   green grid run against a package that did not contain it.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
VER="$(sed -n 's/^pkgver=\(.*\)$/\1/p' "$HERE/PKGBUILD")"

# Args: the first is the output directory, the rest go to makepkg. Pull
# --allow-dirty out of the makepkg arguments: it describes the tarball, and
# makepkg would only reject it as an unknown option.
OUT=""
MAKEPKG_ARGS=()
ALLOW_DIRTY=0
for a in "$@"; do
  case "$a" in
    --allow-dirty) ALLOW_DIRTY=1 ;;
    *) if [ -z "$OUT" ]; then OUT="$a"; else MAKEPKG_ARGS+=("$a"); fi ;;
  esac
done
OUT="${OUT:-$ROOT/release-out}"

pack_worktree() {   # $1 = destination dir, tree comes from $ROOT
  mkdir -p "$1"
  # Tracked files at their working-tree content (a `git archive HEAD` would
  # drop an edit that is not committed yet), then the untracked-and-not-ignored
  # new files - a fix is usually both, and taking only one gave a tree that
  # configured (CMakeLists came from HEAD) and then failed on a missing source
  # file, or built without the change at all.
  { git -C "$ROOT" ls-files -z
    git -C "$ROOT" ls-files -co --exclude-standard -z; } \
    | ( cd "$ROOT" && tar -cf - --null -T - ) \
    | tar -xf - -C "$1"
}

if [ "$ALLOW_DIRTY" = 1 ]; then
  DIRTY=$(git -C "$ROOT" status --porcelain | wc -l)
  echo "== packing the WORKING TREE, not HEAD: $DIRTY path(s) differ from the commit"
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  pack_worktree "$STAGE/traduku-${VER}"
  rm -rf "$OUT"; mkdir -p "$OUT"
  tar -cf - -C "$STAGE" "traduku-${VER}" | gzip > "$OUT/traduku-${VER}.tar.gz"
  echo "   (a real release comes from a clean tree: drop --allow-dirty)"
else
  if [ -n "$(git -C "$ROOT" status --porcelain)" ]; then
    cat >&2 <<EOS
mkrelease.sh: the working tree is dirty, and a release built from it would be
unreproducible - nobody could rebuild this tarball from the commit it claims
to be. Commit, or pass --allow-dirty if you mean a development build.
EOS
    exit 1
  fi
  rm -rf "$OUT"; mkdir -p "$OUT"
  git -C "$ROOT" archive --format=tar.gz \
      --prefix="traduku-${VER}/" HEAD \
      -o "$OUT/traduku-${VER}.tar.gz"
fi
cp "$HERE/PKGBUILD" "$OUT/"
# makepkg resolves install= (and any .install hook) relative to the directory
# it reads the PKGBUILD from, not from $HERE: copying only the PKGBUILD gave
# "install file (traduku-gnome.install) does not exist" and - because the
# caller did not check for a package - the grid then tested whatever was
# already installed. Any auxiliary file the PKGBUILD names comes along.
for aux in "$HERE"/*.install "$HERE"/*.hook; do
  [ -f "$aux" ] && cp "$aux" "$OUT/"
done
# --sign: detached .pkg.tar.zst.sig next to the package — pacman -U verifies
# it automatically when installing (the release channel ships both files).
(cd "$OUT" && makepkg -sf --noconfirm --needed --sign "${MAKEPKG_ARGS[@]+"${MAKEPKG_ARGS[@]}"}")
echo
echo "Release artifacts in $OUT:"
ls -1 "$OUT"
