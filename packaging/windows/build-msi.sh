#!/bin/bash
# The Windows .msi: the same staging tree build-portable.sh fills, wrapped in
# an installer that appears in "Installed apps" and can be uninstalled.
#
# The portable zip stays the primary artifact - it is what a user without
# administrator rights can use, and it is what 0.4 shipped. This adds the other
# shape, for the people who expect software to arrive installably. Both come
# from one staging tree, so the binary inside is the binary the tests ran.
#
# What is checked, in order, with the reason each is here:
#
#  * the licence dialog shows the repository's own LICENSE, converted to RTF at
#    build time - a dialog that shows a paraphrase of the GPL is the installer
#    arguing with the licence;
#  * the file list is harvested from the staging tree, never written by hand:
#    the moment a package carries a list that has to be edited when a DLL
#    appears, it ships yesterday's list and the user gets "Qt6Svg.dll not
#    found" from a green build;
#  * the finished .msi is read back by msi-inspect.ps1, through msi.dll - the
#    reader on the user's machine, not the writer here. File table vs the tree,
#    embedded cabinet, the Upgrade row, per-machine package type, the OS floor,
#    the conditional desktop shortcut. candle exits 0 for a package that
#    installs the wrong thing; this does not.
#  * when the shell is elevated (and only then) the package is installed for
#    real, the installed tree runs `--selftest` with no Qt on PATH, the entry
#    in "Installed apps" is read out of HKLM, and the uninstall removes what
#    the install put down. Without elevation the script prints the skip and its
#    reason, because a release test that silently skips is how 0.4.0's zip
#    stayed broken after every check passed.
#
# Usage: build-msi.sh [--stage DIR] [--out DIR] [--keep] [--no-install] [--prompt-uac]
#        [--wix BIN-DIR]
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
stage="$root/dist/portable"
out="$root/dist"
work=""
keep=0
do_install=auto
wixbin="${WIX_BIN:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        --stage)     stage="$2"; shift 2 ;;
        --out)       out="$2"; shift 2 ;;
        --keep)      keep=1; shift ;;
        --no-install) do_install=0; shift ;;
        --prompt-uac) do_install=prompt; shift ;;
        --wix)       wixbin="$2"; shift 2 ;;
        *) echo "usage: $0 [--stage DIR] [--out DIR] [--keep] [--no-install] [--prompt-uac] [--wix BIN-DIR]" >&2; exit 2 ;;
    esac
done

fail() { echo "FAIL: $*" >&2; exit 1; }
note() { echo "$*"; }

# --- the tool -----------------------------------------------------------------
# WiX Toolset v3 (candle/light/heat), two ways to have it:
#   a) the installer: winget install --id WiXToolset.WiXToolset  (needs UAC)
#   b) no install at all, and the one that works on a machine where you are not
#      admin - the toolset ships as a plain zip of its binaries:
#        curl -sL -o wix.zip https://github.com/wixtoolset/wix3/releases/download/wix3141rtm/wix314-binaries.zip
#        tar -xf wix.zip -C "$LOCALAPPDATA/wix314"
#      WiX v5 (dotnet tool install --global wix) is a real option too; it has a
#      different schema and CLI, and product.wxs here is v3. "wix not found" is
#      not an instruction, which is why this block spells the commands out.
find_wix() {
    local d
    for d in "$wixbin" \
             "$LOCALAPPDATA/wix314" \
             "$LOCALAPPDATA/WiX" \
             "/c/Program Files (x86)/WiX Toolset v3.14/bin" \
             "/c/Program Files/WiX Toolset v3.14/bin" \
             "$(dirname "$(command -v candle.exe 2>/dev/null || echo /nonexistent)")"; do
        [ -n "$d" ] || continue
        if [ -x "$d/candle.exe" ] && [ -x "$d/light.exe" ] && [ -x "$d/heat.exe" ]; then
            echo "$d"; return 0
        fi
    done
    return 1
}

case "$(uname -s)" in
MSYS*|MINGW*|CYGWIN*) on_windows=1 ;;
*) on_windows=0 ;;
esac
[ "$on_windows" = 1 ] || fail "an .msi is written on Windows: this is a Windows Installer database, and the tool that writes it (WiX) runs there. The zip in the same release is the Windows artifact built on Windows; building the msi on Linux would be a package nobody has installed."

wixbin="$(find_wix)" \
    || fail "WiX Toolset v3 not found (candle.exe, light.exe, heat.exe). Either:
    winget install --id WiXToolset.WiXToolset
or, with no admin rights, unpack the binaries into the profile:
    curl -sL -o wix.zip https://github.com/wixtoolset/wix3/releases/download/wix3141rtm/wix314-binaries.zip
    mkdir -p \"\$LOCALAPPDATA/wix314\" && tar -xf wix.zip -C \"\$LOCALAPPDATA/wix314\"
Then re-run, or point this script at it: --wix '/c/Program Files (x86)/WiX Toolset v3.14/bin' or WIX_BIN=..."
candle="$wixbin/candle.exe"; light="$wixbin/light.exe"; heat="$wixbin/heat.exe"
note "wix   $wixbin"

# --- the payload --------------------------------------------------------------
[ -d "$stage" ] || fail "no staging tree at $stage (packaging/windows/build-portable.sh fills it - it deploys the Qt and MinGW runtime by name, and the msi must carry exactly that tree, not a directory copy of a build folder)"
[ -f "$stage/traduku.exe" ] || fail "$stage/traduku.exe missing"
stage="$(cd "$stage" && pwd)"

version="$("$stage/traduku.exe" --version | sed -n 's/^traduku \([0-9.]*\).*/\1/p')"
[ -n "$version" ] || fail "could not read the version from the staged binary"
# MSI versions are three-part numbers with each part <= 65535; a suffix like
# "0.4.0-rc1" is not a package version, and saying so beats letting light
# produce a package whose ProductVersion Windows ignores.
case "$version" in
[0-9]*.[0-9]*.[0-9]*) ;;
*) fail "the package version must be numeric major.minor.patch, got '$version'" ;;
esac
manufacturer="traduku"
# Permanent. Every traduku .msi ever built carries this UpgradeCode, and it is
# the only thing that makes 0.5 replace 0.4. A new GUID here does not "version
# the installer": it produces two installed products that both claim to be
# traduku. Changing it is a decision with a name, and the name is "the
# upgrade path is broken for everyone already installed".
upgradecode="3465F681-627E-4794-BD55-F1AF9FC134CE"
msi="traduku-$version-x64.msi"

work="$(mktemp -d)"
cleanup() { [ "$keep" = 1 ] && note "kept  $work" || rm -rf "$work"; }
trap cleanup EXIT

# --- the licence, as RTF ------------------------------------------------------
# WixUI_Minimal's first dialog is the licence, and WiX wants Rich Text. The
# conversion is here rather than as a committed .rtf so that LICENSE stays the
# single licence: an .rtf copy in the tree is a licence that goes stale the
# first time the real one is edited.
license_rtf="$work/license.rtf"
py="$(command -v python || command -v python3 || true)"
[ -n "$py" ] || fail "python is needed to convert LICENSE to the RTF the licence dialog shows"
"$py" - "$root/LICENSE" "$license_rtf" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
text = open(src, encoding='utf-8', newline='').read().replace('\r\n', '\n')
def esc(c):
    if c == '\\': return '\\\\'
    if c in '{}': return '\\' + c
    o = ord(c)
    if o < 128: return c
    # RTF \ansicpg1252: the dialog font cannot draw everything, so escape the
    # non-ASCII that the GPL text does use (section signs, em dashes) as
    # \'xx in cp1252, and drop what cp1252 has no byte for.
    try:
        b = c.encode('cp1252')
    except UnicodeEncodeError:
        return '?'
    return ''.join('\\x%02x' % x for x in b)
body = '\n'.join(''.join(esc(c) for c in line) + '\\par' for line in text.split('\n'))
rtf = ('{\\rtf1\\ansi\\ansicpg1252\\deff0\\deflang1033'
       '{\\fonttbl{\\f0\\fnil\\fcharset0 Courier New;};}\\f0\\fs16 '
       + body + '}')
with open(dst, 'wb') as f:
    f.write(rtf.encode('ascii'))
PY
[ -s "$license_rtf" ] || fail "the licence RTF came out empty"
note "lic   $(wc -l < "$root/LICENSE") licence lines -> licence.rtf"

# --- the file list, harvested -------------------------------------------------
stage_w="$(cygpath -w "$stage")"
rtf_w="$(cygpath -w "$license_rtf")"
"$heat" dir "$stage_w" \
    -cg RuntimeFiles -dr INSTALLFOLDER \
    -gg -64 -srd -sreg -scom -sdep \
    -var var.SourceDir \
    -o "$(cygpath -w "$work/files.wxs")" >/dev/null || fail "heat could not harvest $stage"
harvested="$(grep -c '<Component ' "$work/files.wxs" || true)"
tree_files="$(find "$stage" -type f | wc -l)"
# The harvest is the payload; a harvest that silently sees fewer files than the
# tree has installs a program that is missing something, and the only way to
# find that out is to compare, here, now.
[ "$harvested" = "$tree_files" ] \
    || fail "heat harvested $harvested components for $tree_files files in $stage - the package would ship a tree that is not the one that was tested"
note "heat  $harvested components, one per staged file"

# --- build --------------------------------------------------------------------
"$candle" -arch x64 -nologo \
    -dProductVersion="$version" -dManufacturer="$manufacturer" \
    -dUpgradeCode="$upgradecode" -dLicenseRtf="$rtf_w" -dSourceDir="$stage_w" \
    -out "$(cygpath -w "$work")\\" \
    "$(cygpath -w "$here/product.wxs")" \
    "$(cygpath -w "$here/shortcuts.wxs")" \
    "$(cygpath -w "$work/files.wxs")" || fail "candle (compile) failed"

mkdir -p "$out"
rm -f "$out/$msi"
"$light" -nologo -ext WixUIExtension -cultures:en-us \
    -out "$(cygpath -w "$out/$msi")" \
    "$(cygpath -w "$work/product.wixobj")" \
    "$(cygpath -w "$work/shortcuts.wixobj")" \
    "$(cygpath -w "$work/files.wixobj")" || fail "light (link) failed"

[ -f "$out/$msi" ] || fail "light reported success and wrote no $out/$msi"

# --- read it back through the engine the user has -----------------------------
ps1="$(cygpath -w "$here/msi-inspect.ps1")"
powershell -NoProfile -ExecutionPolicy Bypass -File "$ps1" \
    -Msi "$(cygpath -w "$out/$msi")" \
    -ExpectFiles "$tree_files" \
    -ExpectUpgradeCode "$upgradecode" \
    || fail "the package does not survive a reading by msi.dll"

# --- install it for real, when this shell can -------------------------------
# The round-trip (install, --selftest from the install dir, "Installed apps"
# entry, uninstall, nothing left) lives in test-msi-install.ps1 and needs
# elevation: a per-machine .msi in a shell that is not elevated answers 1603,
# and "install failed with 1603" in a test log is a statement about the shell,
# not about the package. So: run it when elevated, ask Windows when told to
# (--prompt-uac, a desktop thing), and say the skip out loud when not.
elevated() {
    powershell -NoProfile -ExecutionPolicy Bypass -Command         '([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)'         2>/dev/null | tr -d '
' | tail -1
}
msi_w="$(cygpath -w "$out/$msi")"
rt_w="$(cygpath -w "$here/test-msi-install.ps1")"
want_install="$do_install"
if [ "$want_install" = auto ]; then
    if [ "$(elevated)" = "True" ]; then want_install=1; else
        note "SKIP  the install round-trip: this shell is not elevated, and a per-machine"
        note "      .msi installs into %ProgramFiles% - msiexec would answer 1603 (or pop a"
        note "      UAC dialog into an unattended test). The structural checks above ran;"
        note "      run this script from an elevated shell, or with --prompt-uac, to install"
        note "      traduku, run --selftest from the install dir, and uninstall for real."
    fi
fi
if [ "$want_install" = 1 ]; then
    if [ "$(elevated)" = "True" ]; then
        powershell -NoProfile -ExecutionPolicy Bypass -File "$rt_w" -Msi "$msi_w"             || fail "the install round-trip failed"
    elif [ "$do_install" = prompt ]; then
        # Elevated child: its console is a new window, so its output is
        # redirected to a file we can print, and the exit code is taken from
        # the process. No output and a zero exit code is not enough proof here:
        # the file must say the last line.
        rout="$(cygpath -w "$work")\elevated.out"
        powershell -NoProfile -ExecutionPolicy Bypass -Command             "$p = Start-Process powershell -Verb RunAs -Wait -PassThru -RedirectStandardOutput '$rout' -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File','$rt_w','-Msi','$msi_w'); exit $p.ExitCode"             || fail "the elevated install round-trip failed (output: $work/elevated.out)"
        [ -f "$work/elevated.out" ] && cat "$work/elevated.out"
        tail -1 "$work/elevated.out" | grep -q 'installs, runs, appears, and uninstalls clean'             || fail "the elevated run did not report a clean round-trip"
    fi
fi

msi_sha="$(cd "$out" && sha256sum "$msi" | cut -d' ' -f1)"
exe_sha="$(cd "$stage" && sha256sum traduku.exe | cut -d' ' -f1)"
printf '%s  %s\n' "$msi_sha" "$msi"           >  "$out/SHA256SUMS.msi"
printf '%s  %s\n' "$exe_sha" "portable/traduku.exe" >> "$out/SHA256SUMS.msi"
echo "ok  $out/$msi"
echo "    sha256 $msi_sha"
echo "    exe    $exe_sha"
