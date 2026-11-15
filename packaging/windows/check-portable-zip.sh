#!/usr/bin/env bash
# Prove a portable zip is extractable and runnable *as a user meets it*.
#
#   usage: check-portable-zip.sh [--no-run] <archive.zip> [staged-dir-to-compare]
#
# Why this exists. The 0.4.0 release shipped a zip that every build check
# passed: bsdtar wrote it, bsdtar read it back, the exe inside matched the exe
# built, and Windows' compressed-folder engine - zipfldr, the code behind a
# double-click and "Extract All" - answered "Compressed (zipped) Folder
# Invalid" because all 40 members were named `./traduku.exe`, `./Qt6Core.dll`,
# ... (the archive was written with `-C <stage> .`). Nothing noticed, because
# the verifier was the tool that wrote the archive. Reader rules are not writer
# rules, and a package is only as good as the reader its users have.
#
# Three layers, from cheapest to closest to the user:
#   1. entry names   - no `./`, no absolute paths, no `..`, no backslashes, and
#                      traduku.exe at the root, so extraction cannot scatter;
#   2. required members - the runtime that makes the package portable (Qt
#                      plugins, the MinGW trio); a zip that opens but is missing
#                      qwindows.dll is a package that fails on first start;
#   3. the user's own extractor - zipfldr via Shell.Application, then
#                      `--selftest` from what it produced, with a PATH that has
#                      no Qt on it. Layer 3 is the layer that matters; 1 and 2
#                      exist to name the reason when 3 says "invalid".
#
# Layer 3 is Windows-only and SKIPS with that reason elsewhere; layers 1-2 run
# anywhere bsdtar (or Python's zipfile, the second opinion) can read an archive.
set -uo pipefail

do_run=1
case "${1:-}" in
    --no-run) do_run=0; shift ;;
esac
archive="${1:-}"
compare="${2:-}"
here="$(cd "$(dirname "$0")" && pwd)"   # run-isolated.sh lives beside this file
[ -n "$archive" ] || { echo "usage: $0 [--no-run] <archive.zip> [staged-dir]" >&2; exit 2; }
[ -f "$archive" ] || { echo "check-portable-zip.sh: no such archive: $archive" >&2; exit 2; }
# zipfldr is a shell namespace API: it addresses files by absolute path, and a
# relative one is not "the same file seen from another directory" - NameSpace()
# returns null and the check reports an invalid archive. The probe that proved
# this to me was a Compress-Archive zip that opens fine by absolute path and
# "cannot be opened as a folder" by relative one: the test was broken, not the
# package. Every path handed to PowerShell below is absolute for that reason.
archive_dir="$(dirname "$archive")"
archive_name="$(basename "$archive")"
archive_abs="$(cd "$archive_dir" && pwd -W 2>/dev/null || pwd)"/$archive_name

fail=0
note() { printf '%s\n' "$*"; }
bad()  { printf 'FAIL %s\n' "$*"; fail=1; }

# --- the listing -------------------------------------------------------------
# bsdtar lists it; Python's zipfile is the second reader, because a rule that
# only the writing tool can check is the bug that shipped 0.4.0.
bsdtar="/c/Windows/System32/tar.exe"
[ -x "$bsdtar" ] || bsdtar="$(command -v tar.exe || true)"
[ -n "${bsdtar:-}" ] && [ -x "${bsdtar:-}" ] || bsdtar="$(command -v tar || true)"
have_py=1
command -v python >/dev/null 2>&1 || command -v python3 >/dev/null 2>&1 || have_py=0

listing=""
if [ -n "${bsdtar:-}" ] && [ -x "${bsdtar:-}" ]; then
    listing="$("$bsdtar" -tf "$archive" 2>&1)" || { bad "bsdtar cannot read the archive: $listing"; }
fi
py_names=""
py_dirs=""
if [ "$have_py" = 1 ]; then
    py="$(command -v python || command -v python3)"
    py_all="$("$py" - "$archive" <<'PY'
import sys, zipfile
try:
    z = zipfile.ZipFile(sys.argv[1])
except Exception as e:
    print("E UNREADABLE " + str(e)); sys.exit(0)
bad = z.testzip()
if bad: print("E BADCRC " + bad)
for i in z.infolist():
    print(("D " if i.is_dir() else "F ") + i.filename)
PY
)"
    py_names="$(printf '%s\n' "$py_all" | sed -n 's/^F //p')"
    py_dirs="$(printf '%s\n' "$py_all" | sed -n 's/^D //p')"
    printf '%s\n' "$py_all" | grep '^E BADCRC' >/dev/null 2>&1 \
        && bad "a member fails its CRC: $(printf '%s\n' "$py_all" | sed -n 's/^E BADCRC //p')"
    printf '%s\n' "$py_all" | grep '^E UNREADABLE' >/dev/null 2>&1 \
        && bad "the archive is not a zip Python can read: $(printf '%s\n' "$py_all" | sed -n 's/^E UNREADABLE //p')"
fi
[ -n "$listing" ] || [ -n "$py_names" ] || { echo "check-portable-zip.sh: no reader for this archive (need bsdtar or python)" >&2; exit 1; }

# bsdtar lists a directory as `platforms`, Python as `platforms/`; strip the
# slash from both so the two lists are names, not two spellings of the same
# name. The rules below then apply to files, and a directory entry is never
# mistaken for a member that lacks a trailing slash.
listing="$(printf '%s\n' "$listing" | sed 's|/$||' | sed '/^$/d')"
py_names="$(printf '%s\n' "$py_names" | sed 's|/$||' | sed '/^$/d')"
py_dirs="$(printf '%s\n' "$py_dirs" | sed 's|/$||' | sed '/^$/d')"
# A directory is a directory if Python (the reader that records the flag) says
# so; bsdtar's listing has no such flag, so its copy of the list drops those
# names through this set.
bsdtar_files="$listing"
if [ -n "$py_dirs" ]; then
    filtered=""
    while IFS= read -r m; do
        [ -n "$m" ] || continue
        printf '%s\n' "$py_dirs" | grep -Fxq "$m" && continue
        filtered="$filtered"$'\n'"$m"
    done <<< "$listing"
    bsdtar_files="$(printf '%s\n' "$filtered" | sed '/^$/d')"
    [ -n "$bsdtar_files" ] || bsdtar_files="$listing"
fi

# The rules, run through every reader that exists: two readers, two layouts, is
# the failure class this file was written for.
n=0
for reader in bsdtar python; do
    case "$reader" in
        bsdtar) names="$bsdtar_files" ;;
        python) names="$py_names" ;;
    esac
    [ -n "$names" ] || continue
    while IFS= read -r m; do
        [ -n "$m" ] || continue
        n=$((n+1))
        case "$m" in
            './'*)  bad "$reader: member '$m' starts with './' - zipfldr (Explorer, Extract All) calls such an archive invalid and shows an empty folder";;
            '/'*)   bad "$reader: absolute member '$m' - it cannot be extracted anywhere the user unzips to";;
            *'..'*) bad "$reader: member '$m' escapes the extraction directory";;
            *'\'*)  bad "$reader: member '$m' contains a backslash - the path separator in a zip is '/'";;
            *':/'*|?:*) bad "$reader: member '$m' carries a drive letter";;
        esac
    done <<< "$names"
done
[ "$n" -gt 0 ] || bad "the archive has no members"
if [ -n "$bsdtar_files" ] && [ -n "$py_names" ]; then
    a="$(printf '%s\n' "$bsdtar_files" | sort)"; b="$(printf '%s\n' "$py_names" | sort)"
    [ "$a" = "$b" ] || bad "the two readers disagree about the member list:
  bsdtar: $(comm -23 <(printf '%s\n' "$a") <(printf '%s\n' "$b") | tr '\n' ' ')
  python: $(comm -13 <(printf '%s\n' "$a") <(printf '%s\n' "$b") | tr '\n' ' ')"
fi

# traduku.exe must sit at the root: an archive that unpacks into a folder
# inside the folder the user just made has the exe two clicks deeper than the
# README says, and the "unzip anywhere, run traduku.exe" install stops being true.
files() {
    [ -n "$bsdtar_files" ] && { printf '%s\n' "$bsdtar_files"; return; }
    printf '%s\n' "$py_names"
}
files | grep -qx 'traduku.exe' \
    || bad "traduku.exe is not at the root of the archive (found: $(files | head -3 | tr '\n' ' '))"

# --- the runtime the package is portable *because of* ------------------------
for required in traduku.exe Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Network.dll \
                platforms/qwindows.dll platforms/qoffscreen.dll \
                libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
    files | grep -qx "$required" \
        || bad "$required is not in the archive - the package is not self-contained, and '$required missing' is the first thing a user meets"
done

# --- the staged tree and the archive are one thing ---------------------------
if [ -n "$compare" ] && [ -d "$compare" ]; then
    while IFS= read -r f; do
        rel="${f#"$compare"/}"; rel="${rel//\\//}"
        files | grep -qx "$rel" || bad "$rel is in the staged tree and not in the archive"
    done < <(cd "$compare" && find . -type f | sed 's|^\./||' | sort)
fi

# --- layer 3: the extractor the user has ------------------------------------
case "$(uname -s)" in
MINGW*|MSYS*|CYGWIN*) on_windows=1 ;;
*) on_windows=0 ;;
esac
if [ "$on_windows" = 0 ]; then
    note "SKIP  zipfldr extraction: not on Windows (layers 1-2 checked the layout)"
elif ! command -v powershell >/dev/null 2>&1; then
    bad "powershell not found: the archive was not checked with the extractor a user has"
else
    ps1="$(mktemp --suffix=.ps1)"
    dest="$(mktemp -d)"
    trap 'rm -f "$ps1"; rm -rf "$dest"' EXIT
    cat > "$ps1" <<'PS'
param([string]$zip,[string]$dst)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $dst | Out-Null
$sh  = New-Object -ComObject Shell.Application
$src = $sh.NameSpace($zip)
if ($null -eq $src) { Write-Output "zipfldr cannot open the archive as a folder"; exit 3 }
$items = $src.Items()
if ($null -eq $items -or $items.Count -eq 0) { Write-Output "zipfldr sees no items"; exit 4 }
$sh.NameSpace($dst).CopyHere($items, 4+16)
for ($i = 0; $i -lt 120; $i++) {
    Start-Sleep -Milliseconds 500
    $n = (Get-ChildItem -Recurse -File -Force $dst -ErrorAction SilentlyContinue | Measure-Object).Count
    if ($n -gt 0 -and $n -eq $prev) { break }
    $prev = $n
}
Write-Output ("extracted " + (Get-ChildItem -Recurse -File -Force $dst | Measure-Object).Count + " files via " + $items.Count + " top-level items")
if (-not (Test-Path (Join-Path $dst 'traduku.exe'))) { Write-Output "traduku.exe is not at the extraction root"; exit 5 }
Write-Output "zipfldr OK"
PS
    out="$(powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$ps1" 2>/dev/null || echo "$ps1")" \
             -zip "$(cygpath -w "$archive_abs" 2>/dev/null || echo "$archive_abs")" \
             -dst "$(cygpath -w "$dest" 2>/dev/null || echo "$dest")" 2>&1)"
    ps_rc=$?
    note "zipfldr: $(printf '%s\n' "$out" | tr -d '\r' | tail -2 | tr '\n' ' ')"
    if [ "$ps_rc" != 0 ]; then
        bad "Windows' own compressed-folder engine cannot extract this archive (rc $ps_rc) - this is the check that sees what a double-click sees"
    elif [ "$do_run" = 1 ]; then
        # The last claim of the package: what zipfldr produced runs with no Qt
        # and no compiler on PATH, which is the definition of portable.
        syspath="$(dirname "$(command -v cmd.exe 2>/dev/null || echo /c/Windows/System32/cmd.exe)")"
        if ! (cd "$dest" && PATH="$syspath" "$BASH" "$here/run-isolated.sh" ./traduku.exe --selftest 2>/dev/null \
                | grep -q '^SELFTEST PASSED'); then
            bad "the exe that zipfldr extracted does not pass --selftest with a PATH that has no Qt"
        else
            note "ok    zipfldr extracted a tree that passes --selftest with no Qt on PATH"
        fi
    fi
fi

if [ "$fail" = 0 ]; then
    note "ok    $(basename "$archive") opens where users open it: $(files | wc -l | tr -d ' ') members, traduku.exe at the root"
fi
exit "$fail"
