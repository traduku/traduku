#!/bin/bash
# Release gate: the Windows .msi, and the checks that read it.
#
# Registered in tests/CMakeLists.txt as `msi`. It runs on Windows only, and it
# is a release gate rather than a unit test for the same reason portable_zip is:
# it measures a package, not a function, and its subject (candle, light,
# msi.dll) is not something a test binary can fake.
#
# The shape of it, following the rule the 0.4.0 zip incident taught
# (ROADMAP.md §7.8 item 5): a gate that only checks the good case measures its
# own optimism. So after the real package is built and inspected, the gate
# writes three deliberate mutants and requires that msi-inspect.ps1 REFUSES
# each one. Each mutant targets one check, so the gate fails when a check stops
# seeing - the failure mode of an inspector is to return "ok" at everything.
#
#   1. EmbedCab="no"       - the two-file package: an .msi whose cabinet is a
#                            sibling. It installs on the build machine, where
#                            the .cab is in the folder, and fails on the user's,
#                            where only the .msi was downloaded.
#   2. the shortcut condition removed - CREATEDESKTOPSHORTCUT=0 becomes a
#                            documented option that does nothing.
#   3. a pruned harvest with the full file count expected - the package ships
#                            fewer files than the tested tree has: "Qt6Svg.dll
#                            is missing" from a green build.
#
# Skips, each with the reason it is allowed to skip: not Windows, no WiX (the
# install commands are in build-msi.sh's header), no staging tree.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
win="$root/packaging/windows"
out="$root/dist"

skip() { echo "SKIP  msi: $*"; exit 0; }
fail() { echo "FAIL  $*" >&2; exit 1; }

case "$(uname -s)" in
MSYS*|MINGW*|CYGWIN*) ;;
*) skip "an .msi is written and installed on Windows (this is $(uname -s))" ;;
esac
[ -f "$win/build-msi.sh" ] || fail "no $win/build-msi.sh"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# --- the real package ---------------------------------------------------------
# $out is the dist directory and $res is the text a check printed. The gate once
# assigned the second into the first, and the failure was a `cd` into 900
# characters of inspection output: short names, one meaning each.
version="$("$out/portable/traduku.exe" --version | sed -n 's/^traduku \([0-9.]*\).*/\1/p')"
[ -n "$version" ] || fail "cannot read the version from $out/portable/traduku.exe (packaging/windows/build-portable.sh stages it)"
name="traduku-$version-x64.msi"

# --no-install: the round-trip needs elevation, and a UAC prompt in the middle
# of `ctest` is not a test, it is a popup. build-msi.sh runs the round-trip on
# its own, from an elevated shell, and prints the skip when it cannot.
if ! "$win/build-msi.sh" --out "$out" --no-install > "$work/build.out" 2>&1; then
    cat "$work/build.out" >&2
    grep -q "WiX Toolset v3 not found" "$work/build.out" \
        && skip "WiX Toolset v3 is not installed (see the header of packaging/windows/build-msi.sh for the two ways to get it)"
    grep -q "no staging tree" "$work/build.out" \
        && skip "no staging tree: packaging/windows/build-portable.sh fills dist/portable, and the msi wraps exactly that tree"
    fail "build-msi.sh failed"
fi
tail -6 "$work/build.out"
[ -f "$out/$name" ] || fail "build-msi.sh reported ok and wrote no $out/$name"

tree_files="$(find "$out/portable" -type f | wc -l)"
upgradecode="$(sed -n 's/^upgradecode="\(.*\)"/\1/p' "$win/build-msi.sh")"
[ -n "$upgradecode" ] || fail "cannot read the UpgradeCode out of build-msi.sh"

inspect() {   # inspect <msi> [extra args...] -> prints, returns the script's rc
    local msi="$1"; shift
    powershell -NoProfile -ExecutionPolicy Bypass \
        -File "$(cygpath -w "$win/msi-inspect.ps1")" -Msi "$(cygpath -w "$msi")" \
        -ExpectFiles "$tree_files" -ExpectUpgradeCode "$upgradecode" "$@" 2>&1
}

res="$(inspect "$out/$name")" || { echo "$res" >&2; fail "the release package does not survive a reading by msi.dll"; }
echo "$res" | tail -3
echo "ok    msi: the release package is a database msi.dll agrees with ($tree_files files, embedded cabinet, one upgrade code, per-machine, x64, OS floor, switchable shortcut)"

# The msi must carry the binary the tests ran, byte for byte.
msi_exe_sha="$(cd "$out/portable" && sha256sum traduku.exe | cut -d' ' -f1)"
built_exe_sha="$(sha256sum "$root/build-win/bin/traduku.exe" 2>/dev/null | cut -d' ' -f1 || true)"
if [ -n "$built_exe_sha" ]; then
    [ "$msi_exe_sha" = "$built_exe_sha" ] \
        || fail "the staged tree the package is built from is not the build output:
  staged $msi_exe_sha
  built  $built_exe_sha"
    echo "ok    msi: traduku.exe inside is the binary this tree built ($msi_exe_sha)"
fi

# --- the mutants: each one targets one check, and must be refused -------------
wixbin=""
for d in "${WIX_BIN:-}" "$LOCALAPPDATA/wix314" "/c/Program Files (x86)/WiX Toolset v3.14/bin"; do
    [ -n "$d" ] && [ -x "$d/light.exe" ] && { wixbin="$d"; break; }
done
[ -n "$wixbin" ] || skip "the mutants: no WiX binaries to rebuild them with"
candle="$wixbin/candle.exe"; light="$wixbin/light.exe"; heat="$wixbin/heat.exe"

# $name and $version came from the staged binary, above the build.
stage="$(cd "$out/portable" && pwd)"; stage_w="$(cygpath -w "$stage")"
"$heat" dir "$stage_w" -cg RuntimeFiles -dr INSTALLFOLDER -gg -64 -srd -sreg -scom -sdep     -var var.SourceDir -o "$(cygpath -w "$work/files.wxs")" >/dev/null     || fail "heat could not harvest the staging tree"

# The licence text is not what the mutants are about, but light puts it in the
# EulaText string and checks it against the database code page - so the mutants
# need a licence file of their own, small and ASCII. (Pointing the variable at
# the finished .msi, which is what I tried first, fails with LGHT0311 on
# "characters not available in code page 1252": the cabinet bytes are not text.
# That error is also the real build's check that the licence is readable by the
# dialog, which is worth knowing.)
# Candle and light run on copies of the .wxs files: the shipped ones stay
# shipped, and each mutation is a diff the gate printed to itself.
license="$work/license.rtf"
printf '{\rtf1\ansi\ansicpg1252 The gate licence text, ASCII by design.}' > "$license"
mutant() {   # mutant <tag> <file-to-patch> <sed-expr> <expected FAIL text>
    local tag="$1" src="$2" expr="$3" expect="$4"
    local d="$work/m-$tag"; mkdir -p "$d"
    sed "$expr" "$win/$src" > "$d/$src"
    cmp -s "$d/$src" "$win/$src"         && fail "mutant '$tag' changed nothing in $src - the sed expression no longer matches the shipped source, so this gate would be testing a mutant that does not exist"
    local a="$win/product.wxs" b="$win/shortcuts.wxs"
    [ "$src" = product.wxs ] && a="$d/product.wxs"
    [ "$src" = shortcuts.wxs ] && b="$d/shortcuts.wxs"
    "$candle" -arch x64 -nologo -dProductVersion="$version" -dManufacturer=traduku         -dUpgradeCode="$upgradecode" -dLicenseRtf="$(cygpath -w "$license")" -dSourceDir="$stage_w"         -out "$(cygpath -w "$d")\\" "$a" "$b" "$(cygpath -w "$work/files.wxs")" >/dev/null 2>&1         || fail "mutant '$tag' does not compile"
    "$light" -nologo -ext WixUIExtension -cultures:en-us -out "$(cygpath -w "$d/mutant.msi")"         "$(cygpath -w "$d/product.wixobj")" "$(cygpath -w "$d/shortcuts.wixobj")"         "$(cygpath -w "$d/files.wixobj")" > "$d/light.log" 2>&1         || fail "mutant '$tag' does not link: $(tail -2 "$d/light.log")"
    local res rc=0
    res="$(inspect "$d/mutant.msi" -Quiet)" || rc=$?
    [ "$rc" -ne 0 ] || fail "mutant '$tag' PASSED the inspection - the check for '$expect' sees nothing any more, and a release test that cannot see the incident is a release test that ships it"
    echo "$res" | grep -qi "$expect"         || fail "mutant '$tag' was refused, but not by the check meant to refuse it (expected a line about '$expect'):
$res"
    echo "ok    msi: mutant '$tag' refused by the '$expect' check"
}

# 1. the two-file package: it installs on the machine that built it, where the
#    .cab is in the folder, and fails on the user's, where only the .msi was
#    downloaded.
mutant twofile product.wxs 's|<MediaTemplate EmbedCab="yes" />|<MediaTemplate EmbedCab="no" />|'     'embedded cabinet'
# 2. the switch that switches nothing.
mutant unconditioned shortcuts.wxs 's|<Condition>CREATEDESKTOPSHORTCUT</Condition>||'     'CREATEDESKTOPSHORTCUT'

# 3. a payload that is not the tree: one plugin the app loads at start removed,
#    with the full file count still claimed. The harvest is the same tool, so
#    this is the shape of "somebody deleted a DLL from the staging directory".
pruned="$work/pruned"; mkdir -p "$pruned"
cp -r "$stage/." "$pruned/"
rm -f "$pruned/platforms/qwindows.dll"
[ -f "$pruned/platforms/qwindows.dll" ] && fail "could not prune the copy of the staging tree"
pruned_w="$(cd "$pruned" && pwd -W 2>/dev/null || pwd)"
"$heat" dir "$pruned_w" -cg RuntimeFiles -dr INSTALLFOLDER -gg -64 -srd -sreg -scom -sdep     -var var.SourceDir -o "$(cygpath -w "$work/files-pruned.wxs")" >/dev/null     || fail "heat on the pruned tree"
d="$work/m-pruned"; mkdir -p "$d"
"$candle" -arch x64 -nologo -dProductVersion="$version" -dManufacturer=traduku     -dUpgradeCode="$upgradecode" -dLicenseRtf="$(cygpath -w "$license")" -dSourceDir="$pruned_w"     -out "$(cygpath -w "$d")\\"     "$(cygpath -w "$win/product.wxs")" "$(cygpath -w "$win/shortcuts.wxs")"     "$(cygpath -w "$work/files-pruned.wxs")" >/dev/null 2>&1 || fail "the pruned mutant does not compile"
"$light" -nologo -ext WixUIExtension -cultures:en-us -out "$(cygpath -w "$d/mutant.msi")"     "$(cygpath -w "$d/product.wixobj")" "$(cygpath -w "$d/shortcuts.wixobj")"     "$(cygpath -w "$d/files-pruned.wixobj")" > "$d/light.log" 2>&1     || fail "the pruned mutant does not link: $(tail -2 "$d/light.log")"
res="$(inspect "$d/mutant.msi" -Quiet)"     && fail "the pruned package PASSED the inspection: a package with platforms\qwindows.dll missing installs successfully and the app never starts - that is the exact failure this gate exists to catch"     || true
echo "$res" | grep -qi "the staging tree has" || { echo "$res" >&2; fail "the pruned package was refused, but not by the harvest-vs-tree check"; }
echo "ok    msi: a package with one plugin fewer than the tree is refused (File table vs the staging tree)"

echo "ok    msi: the package, and the checks themselves"
