#!/usr/bin/env bash
# Gate: a portable zip must be extractable by the extractor its users have.
#
# The accident. The 0.4.0 Windows release carried an archive that every build
# check had passed: bsdtar wrote it, bsdtar read it back, the exe inside matched
# the exe built, `--selftest` passed from the unpacked tree - and
# `traduku-0.4.0-win64-portable.zip` could not be extracted on Windows at all.
# The build had packed it with `-C <stage> .`, so all 40 members were named
# `./traduku.exe`, `./Qt6Core.dll`, ..., and zipfldr - the code behind a
# double-click and "Extract All" - answers "Compressed (zipped) Folder Invalid"
# and shows an empty folder. The release page's only Windows package was an
# archive you can download and cannot open.
#
# The shape of the bug, stated once: the verifier was the tool that wrote the
# archive. bsdtar accepts `./` silently; the user's reader does not. So this gate
# does three things a build check does not:
#   * packs a synthetic tree through the real writer (packaging/windows/mkzip.sh),
#     so the writer, not a copy of its flags, is under test;
#   * checks the result with the reader rules (check-portable-zip.sh), including
#     zipfldr on Windows;
#   * packs the same tree the way the release was packed and REQUIRES the check
#     to refuse it. A gate that cannot reproduce the incident is measuring its
#     own optimism, and every row of this suite exists because someone learned
#     that the hard way.
#
# No Qt, no compiler, no traduku.exe built: the archive layout is a packaging
# fact, and it is checkable in a container with one file and nine directories.
#
#   usage: test_portable_zip.sh <repo>
set -uo pipefail

repo="${1:-.}"
cd "$repo"
here="$repo/packaging/windows"
for tool in "$here/mkzip.sh" "$here/check-portable-zip.sh"; do
    [ -f "$tool" ] || { echo "FAIL $tool is missing - this gate tests it by name"; exit 1; }
done

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
fail=0

# --- a staged tree shaped like a deployed package ----------------------------
stage="$work/stage"
mkdir -p "$stage/platforms" "$stage/imageformats"
printf 'MZ fake exe\n'      > "$stage/traduku.exe"
printf 'fake\n'             > "$stage/Qt6Core.dll"
printf 'fake\n'             > "$stage/Qt6Gui.dll"
printf 'fake\n'             > "$stage/Qt6Widgets.dll"
printf 'fake\n'             > "$stage/Qt6Network.dll"
printf 'fake\n'             > "$stage/libgcc_s_seh-1.dll"
printf 'fake\n'             > "$stage/libstdc++-6.dll"
printf 'fake\n'             > "$stage/libwinpthread-1.dll"
printf 'fake\n'             > "$stage/platforms/qwindows.dll"
printf 'fake\n'             > "$stage/platforms/qoffscreen.dll"
printf 'fake\n'             > "$stage/imageformats/qjpeg.dll"

# --- 1. the writer, checked by the reader rules ------------------------------
good="$work/good.zip"
if ! "$here/mkzip.sh" "$good" "$stage" > "$work/mkzip.log" 2>&1; then
    echo "FAIL mkzip.sh refused to write an archive:"; sed 's/^/     /' "$work/mkzip.log"
    fail=1
fi
if [ "$fail" = 0 ]; then
    # --no-run: this is a fake exe, so layer 3's "it starts" claim is not
    # available here. The layout claims are, and they are the incident.
    if ! "$here/check-portable-zip.sh" --no-run "$good" "$stage" > "$work/check.log" 2>&1; then
        echo "FAIL the archive the release writer produces is refused by the release check:"
        sed 's/^/     /' "$work/check.log"; fail=1
    fi
fi

# --- 2. negative control: the shipped layout must be REFUSED ------------------
old="$work/old.zip"
bsdtar="/c/Windows/System32/tar.exe"
[ -x "$bsdtar" ] || bsdtar="$(command -v tar.exe || true)"
if [ -n "${bsdtar:-}" ] && [ -x "${bsdtar:-}" ]; then
    (cd "$stage" && rm -f "$old" && "$bsdtar" -a -cf "$old" -C "$stage" .) >/dev/null 2>&1 \
        || (cd "$stage" && rm -f "$old" && "$bsdtar" -a -cf "$old" .) >/dev/null 2>&1
    if [ ! -f "$old" ]; then
        echo "FAIL cannot reproduce the 0.4.0 archive layout (bsdtar refused to pack '.')"
        fail=1
    elif "$here/check-portable-zip.sh" --no-run "$old" > "$work/old.log" 2>&1; then
        echo "FAIL the release check accepted the exact layout that broke the 0.4.0 release"
        echo "     a gate that cannot reproduce the incident is not guarding against it"
        sed 's/^/     /' "$work/old.log"
        fail=1
    else
        grep -q "'\./'" "$work/old.log" \
            || echo "     note: it was refused, but not for the './' reason - check the message"
        echo "ok    the './' layout of the 0.4.0 release is refused by the check"
    fi
else
    echo "SKIP the negative control: no bsdtar here to write the 0.4.0-style archive"
fi

# --- 3. a missing runtime is a broken package, not a smaller one -------------
thin="$work/thin.zip"
mkdir -p "$work/thinroot"; cp -r "$stage" "$work/thin"
rm -f "$work/thin/platforms/qwindows.dll"
"$here/mkzip.sh" "$thin" "$work/thin" >/dev/null 2>&1
if "$here/check-portable-zip.sh" --no-run "$thin" > "$work/thin.log" 2>&1; then
    echo "FAIL the check accepted an archive without platforms/qwindows.dll"
    echo "     that package opens and dies on the first start, which is the report users file"
    fail=1
else
    echo "ok    a package missing a shipped runtime file is refused"
fi

# --- 4. the real artifact, when this tree has one ----------------------------
# The gate must guard the file that gets uploaded, so when a build produced a
# package, it is checked with everything above plus the claim that the extracted
# exe runs (--selftest, no Qt on PATH).
real="$(find dist -maxdepth 1 -name 'traduku-*-win64-portable.zip' 2>/dev/null | head -1)"
if [ -n "$real" ]; then
    if "$here/check-portable-zip.sh" "$real" > "$work/real.log" 2>&1; then
        echo "ok    $real passes the release checks as built"
    else
        echo "FAIL $real is refused by the release checks:"
        sed 's/^/     /' "$work/real.log"; fail=1
    fi
else
    echo "SKIP  $real: no package built in this tree (packaging/windows/build-portable.sh makes one)"
fi

[ "$fail" = 0 ] && echo "ok    portable zip layout: writer, reader rules, zipfldr, runtime membership"
exit "$fail"
