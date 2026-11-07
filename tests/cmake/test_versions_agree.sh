#!/bin/sh
# The version lives in three files that nothing forces to agree, and they had
# already drifted when this test was written: CMakeLists said 0.2.0, the
# PKGBUILD said 0.2, and the changelog opened with 0.2 while an older entry
# below it said 0.2.0-1 (dpkg complained: "the current version (0.2) is
# earlier than the previous one").
#
# Why it matters to a user and not just to tidy: pacman and apt both refuse to
# "upgrade" to a version they already have. A release that forgets to bump
# ships a package that installs but never arrives on an installed machine - and
# a --version that disagrees with the package DB makes every bug report
# ambiguous, which is the exact confusion this project has already paid for.
#
# Usage: test_versions_agree.sh <source dir>
set -u
src="$1"
fail() { echo "FAIL: $*" >&2; exit 1; }

cmake_ver=$(sed -n 's/^project(traduku VERSION \([0-9.]*\).*/\1/p' "$src/CMakeLists.txt")
pkg_ver=$(sed -n 's/^pkgver=\(.*\)$/\1/p' "$src/packaging/arch/PKGBUILD")
# Debian revision dropped: the changelog opens with "traduku (0.3.0-1)".
chlog_ver=$(sed -n 's/^traduku (\([0-9][0-9A-Za-z.~+-]*\).*/\1/p' "$src/packaging/debian/changelog" | head -1)
chlog_ver=${chlog_ver%%-*}

[ -n "$cmake_ver" ]  || fail "no VERSION in CMakeLists.txt project()"
[ -n "$pkg_ver" ]    || fail "no pkgver in packaging/arch/PKGBUILD"
[ -n "$chlog_ver" ]  || fail "no version in packaging/debian/changelog"

[ "$cmake_ver" = "$pkg_ver" ] \
    || fail "version drift: CMakeLists says $cmake_ver, the PKGBUILD says $pkg_ver - pacman will install a package whose --version contradicts its own database"
[ "$cmake_ver" = "$chlog_ver" ] \
    || fail "version drift: CMakeLists says $cmake_ver, the changelog opens with $chlog_ver"

# The changelog must be newest-first; dpkg refuses to build a package whose
# entries descend, and it does so with a warning people ignore.
if command -v dpkg --help >/dev/null 2>&1 && dpkg --compare-versions 0 2>/dev/null; then
    prev=""
    # a `for`, not a `while read` on a pipe: the loop body must run in this
    # shell, or `fail` inside it only exits a subshell and the bad order is
    # reported as success.
    for v in $(sed -n 's/^traduku (\([0-9][0-9A-Za-z.~+-]*\).*/\1/p' "$src/packaging/debian/changelog"); do
        if [ -n "$prev" ] && ! dpkg --compare-versions "$prev" ge "$v"; then
            fail "changelog out of order: $prev is listed above the newer $v"
        fi
        prev="$v"
    done
fi

# The binary name the package installs must be what the tests and the desktop
# file invoke; a rename that misses one of them ships an app that starts from
# no menu entry.
grep -q '^Exec=traduku' "$src/packaging/gnome/traduku.desktop" \
    || fail "packaging/gnome/traduku.desktop does not Exec the installed binary"

# One identity, four spellings (autostart file name, .desktop file name,
# WM_CLASS Qt derives, StartupWMClass). The code says it once, in
# lt::desktopBaseName(); the packaging file has to agree with it, or
# gnome-shell shows the window as a second nameless icon next to the launcher.
base=$(sed -n 's/.*desktopBaseName()\s*$//;s/.*return QStringLiteral("\([^"]*\)");.*/\1/p' \
        "$src/src/app/AppFlavour.cpp" | head -1)
[ -n "$base" ] || fail "could not read lt::desktopBaseName() from AppFlavour.cpp"
for desktop in "$src/packaging/gnome/$base.desktop" "$src/$base.desktop"; do
    # Both shells ship a .desktop (the KDE one at the repo root), and both are
    # matched by the same WM_CLASS Qt produces - a fix on one shell only is
    # the divergence this file exists to catch.
    test -f "$desktop" || continue
    grep -q "^StartupWMClass=$base$" "$desktop" \
        || fail "$desktop lacks StartupWMClass=$base (window and launcher would not pair)"
done
test -f "$src/packaging/gnome/$base.desktop" \
    || fail "packaging/gnome/$base.desktop: the file name is part of the identity"

# The README prints the file name a user is going to hand to pacman and apt.
# That makes its version a promise executed by hand, and it had drifted: the
# install section told people about `traduku-0.1-1-x86_64.pkg.tar.zst` while
# the build produced 0.3.0 - a copy-paste that cannot work and reads like the
# project is two releases behind its own docs. Only artifact lines are read,
# so "Qt 6.4+" and "CMake >= 3.21" stay out of it.
if [ -f "$src/README.md" ]; then
    # Match artifact FILE NAMES, not any number on the line. The first version
    # of this check took `[0-9]+\.[0-9]+` from any line mentioning a package,
    # and went red on "…18 passed / 5 skipped, 103.0 s" - a timing that happens
    # to sit on the same row as a .deb name. A check that cries at numbers is a
    # check people widen until it stops talking.
    #
    # Shape: <name><-|_><version><-|_><revision><-|_><arch>.<deb|pkg.tar.zst>
    artifacts=$(grep -oE '[A-Za-z0-9._-]+[-_][0-9]+\.[0-9.]*[-_][0-9A-Za-z.~+-]+[-_](amd64|x86_64)\.(deb|pkg\.tar\.zst)' "$src/README.md" 2>/dev/null)
    if [ -n "$artifacts" ]; then
        for a in $artifacts; do
            v=$(printf '%s\n' "$a" \
                 | sed -E 's/^[A-Za-z0-9._-]+[-_]([0-9]+\.[0-9.]*[0-9])[-_].*$/\1/')
            [ "$v" = "$cmake_ver" ] || fail "README.md pins artifact version $v (in $a) while the packages are $cmake_ver - the install command it prints names a file nothing builds"
        done
        # A row that mentions a package but yields no filename is the case the
        # old grep was actually guarding: an install line that names a file with
        # no version at all cannot be typed by a human.
        printf '%s\n' "$artifacts" | grep -qE '\.(deb|pkg\.tar\.zst)$' \
            || fail "README.md names a package file with no version"
    fi
fi

echo ok
