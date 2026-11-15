#!/bin/bash
# Release gate: install the .msi for real, make the installed program translate,
# uninstall it, and prove the row can see a broken package.
#
# Registered as `msi_e2e`. It is the pipeline version of "double-click the
# installer and use the app": the artifact under test is the file on the release
# page, and the assertions are the user's - it installs, the program that lands
# in %ProgramFiles% talks to an API, it appears in "Installed apps", and removing
# it removes it.
#
# ELEVATION, and why a skip is the right answer here. The package is
# per-machine (product.wxs: a global-hotkey tray app is one program per
# machine), so installing it writes to %ProgramFiles% and HKLM, which needs an
# elevated token. CI Windows runners run their jobs elevated, so on a pipeline
# this row executes. On a developer's shell it does not, and msiexec answers
# 1603 - "installation failed" - for a package that is fine: a red row that
# measured the shell instead of the package is worse than no row, so this script
# checks for the token first and prints the skip, the reason, and the command
# that runs the row for real.
#
# THE CONTROLS. The repo rule (ROADMAP.md §7.8 items 5 and 6): a gate that
# cannot reproduce the incident measures its own optimism. Two mutants here:
#
#   A. the same package with the cabinet NOT embedded - the two-file package.
#      The install must FAIL. If this row passes on it, the row is not measuring
#      installation; it is measuring msiexec's willingness to be called.
#   B. the install with the translate stage pointed at a port where nothing
#      listens. It must FAIL. If it passes, "the installed program translates"
#      is a string the script prints, not a thing it checked.
#
# Both controls install and uninstall, so they need elevation too, and they run
# only when the real row can.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
win="$root/packaging/windows"
out="$root/dist"

skip() { echo "SKIP  msi_e2e: $*"; exit 0; }
fail() { echo "FAIL  msi_e2e: $*" >&2; exit 1; }

case "$(uname -s)" in
MSYS*|MINGW*|CYGWIN*) ;;
*) skip "an .msi installs on Windows (this is $(uname -s))" ;;
esac
[ -f "$win/test-msi-install.ps1" ] || fail "no $win/test-msi-install.ps1"
[ -f "$win/build-msi.sh" ] || fail "no $win/build-msi.sh"

# --- the package under test: built by this tree, not downloaded --------------
[ -x "$out/portable/traduku.exe" ] \
    || skip "no staged tree in $out (packaging/windows/build-portable.sh fills it, and the msi wraps exactly that tree)"
version="$("$out/portable/traduku.exe" --version | sed -n 's/^traduku \([0-9.]*\).*/\1/p')"
[ -n "$version" ] || fail "cannot read the version from the staged binary"
msi="$out/traduku-$version-x64.msi"
if [ ! -f "$msi" ]; then
    echo "note  building $msi first"
    "$win/build-msi.sh" --out "$out" --no-install > "$out/.msi-e2e-build.log" 2>&1 \
        || { tail -6 "$out/.msi-e2e-build.log" >&2; grep -q "WiX Toolset v3 not found" "$out/.msi-e2e-build.log" \
                && skip "WiX Toolset v3 is not installed, so no package exists to install (see the header of packaging/windows/build-msi.sh)"; }
fi
[ -f "$msi" ] || fail "no $msi and no way to build it"

elevated() {
    powershell -NoProfile -ExecutionPolicy Bypass -Command \
        '([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)' \
        2>/dev/null | tr -d '\r' | tail -1
}
if [ "$(elevated)" != "True" ]; then
    skip "this shell is not elevated. A per-machine .msi writes %ProgramFiles% and HKLM; msiexec answers 1603 to an unelevated install, and that number describes the shell, not the package. On a CI Windows runner the job token is elevated and this row runs. To run it here:
    powershell -NoProfile -ExecutionPolicy Bypass -File $(cygpath -w "$win/test-msi-install.ps1") -Msi $(cygpath -w "$msi")
in an elevated PowerShell, or: packaging/windows/build-msi.sh --prompt-uac"
fi

run_ps() {   # run_ps <msi> [extra args...] -> prints, returns the script rc
    local msi="$1"; shift
    powershell -NoProfile -ExecutionPolicy Bypass \
        -File "$(cygpath -w "$win/test-msi-install.ps1")" \
        -Msi "$(cygpath -w "$msi")" -MockPort "$((- 1))" "$@" 2>&1
    # -MockPort -1 is not a port: it tells the script "pick one", which is the
    # script's own default. Passing a number here keeps each run isolated from a
    # mock server a previous crashed run may have left on the default port.
}

echo "=== the package: $msi"
res="$(run_ps "$msi")" || { echo "$res" >&2; fail "the install round-trip failed"; }
echo "$res"
echo "$res" | grep -q 'translated through the network stack the package carries' \
    || fail "the round-trip passed without the translate stage running - the row is not measuring what its name says"
echo "ok    msi_e2e: installed, --selftest from the install dir, translated against the mock, listed in Installed apps, uninstalled with nothing left"

# --- control A: a package that cannot install, and the row must say so --------
wixbin=""
for d in "${WIX_BIN:-}" "$LOCALAPPDATA/wix314" "/c/Program Files (x86)/WiX Toolset v3.14/bin"; do
    [ -n "$d" ] && [ -x "$d/light.exe" ] && { wixbin="$d"; break; }
done
[ -n "$wixbin" ] || skip "the controls: no WiX binaries to build the mutant packages with"

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
stage="$(cd "$out/portable" && pwd)"; stage_w="$(cygpath -w "$stage")"
license="$work/license.rtf"
printf '{\\rtf1\\ansi\\ansicpg1252 Control licence text.}' > "$license"
"$wixbin/heat.exe" dir "$stage_w" -cg RuntimeFiles -dr INSTALLFOLDER -gg -64 -srd -sreg -scom -sdep \
    -var var.SourceDir -o "$(cygpath -w "$work/files.wxs")" >/dev/null || fail "heat"
sed 's|<MediaTemplate EmbedCab="yes" />|<MediaTemplate EmbedCab="no" />|' "$win/product.wxs" > "$work/twofile.wxs"
"$wixbin/candle.exe" -arch x64 -nologo -dProductVersion="$version" -dManufacturer=traduku \
    -dUpgradeCode="$(sed -n 's/^upgradecode="\(.*\)"/\1/p' "$win/build-msi.sh")" \
    -dLicenseRtf="$(cygpath -w "$license")" -dSourceDir="$stage_w" \
    -out "$(cygpath -w "$work")\\" \
    "$(cygpath -w "$work/twofile.wxs")" "$(cygpath -w "$win/shortcuts.wxs")" \
    "$(cygpath -w "$work/files.wxs")" >/dev/null || fail "control A: the mutant does not compile"
"$wixbin/light.exe" -nologo -ext WixUIExtension -cultures:en-us -out "$(cygpath -w "$work/twofile.msi")" \
    "$(cygpath -w "$work/twofile.wixobj")" "$(cygpath -w "$work/shortcuts.wixobj")" \
    "$(cygpath -w "$work/files.wixobj")" > "$work/light.log" 2>&1 || fail "control A: the mutant does not link: $(tail -2 "$work/light.log")"
res="$(powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$win/test-msi-install.ps1")" \
        -Msi "$(cygpath -w "$work/twofile.msi")" -NoTranslate 2>&1)" && rc=0 || rc=$?
[ "$rc" -ne 0 ] || fail "control A PASSED: a package whose cabinet is not embedded installed cleanly on this machine, which means the row measures msiexec being called, not files being placed. (It would fail on a user's machine, where only the .msi was downloaded.)"
echo "ok    msi_e2e: control A - the two-file package is refused at install time (rc $rc)"

# --- control B: the translate stage with nothing to translate against --------
res="$(powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$win/test-msi-install.ps1")" \
        -Msi "$(cygpath -w "$msi")" -MockPort 1 2>&1)" && rc=0 || rc=$?
[ "$rc" -ne 0 ] || fail "control B PASSED: the translate stage reported success with no server listening on the port - 'the installed program translates' is then a sentence, not a check"
echo "$res" | grep -qi 'mock endpoint never answered' \
    || { echo "$res" >&2; fail "control B was refused for a reason other than the dead endpoint"; }
echo "ok    msi_e2e: control B - the translate stage fails when the endpoint is not there (rc $rc)"

echo "ok    msi_e2e: the install, and two ways the row can see it broken"
