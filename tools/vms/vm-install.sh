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
T0=$SECONDS          # phase timings land in the last line: the grid cycle was
                     # being optimised blind (ROADMAP.md §4.6 - measure first)
VER=$(sed -n 's/^pkgver=\(.*\)$/\1/p' "$ROOT/packaging/arch/PKGBUILD")

# Which flavour this guest must run. The two shells are different packages
# (traduku = Plasma/KF6, traduku-gnome = GNOME with no KF6 at all), and
# installing the wrong one is not cosmetic: the GNOME guest then needs KF6 it
# does not have, the Plasma guest loses KF6 notifications. So the flavour
# comes from the profile name - and what actually landed is verified below,
# not trusted, because "which build is in front of me" has already cost this
# project two misleading reports.
case "$VM" in
  *-gnome) FLAVOUR=gnome ;;
  *-kde)   FLAVOUR=kde ;;
  *) echo "unknown vm profile $VM (expected *-kde or *-gnome)" >&2; exit 1 ;;
esac
PKGNAME=traduku
[ "$FLAVOUR" = gnome ] && PKGNAME=traduku-gnome
PKG=$ROOT/release-out/${PKGNAME}-${VER}-1-x86_64.pkg.tar.zst
SSH="ssh -i $HOME/vms/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
IDX=$("$HERE/vmctl.sh" status 2>/dev/null | awk -v v="$VM" '$1==v{print $3}' | tr -d 'idx=')
SSH="$SSH -p 220${IDX: -1}"
Q() { $SSH vm@127.0.0.1 "$@"; }
SCP() { scp -i "$HOME/vms/id_ed25519" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -P "220${IDX: -1}" "$@"; }

# A VM that was just started - or reset and started, which is how a grid run
# begins - has no sshd yet, and the first ssh of the install then died with
# "Connection reset by peer", aborting the whole install before it built
# anything. This waits, and it says so: a tool that stalls silently is
# indistinguishable from one that hung, and the last thing the grid needs is a
# step whose waiting is invisible.
wait_for_ssh() {
  local tries=${1:-90} i=0
  until Q "echo up" | grep -q up; do
    i=$((i + 1))
    if [ "$i" -ge "$tries" ]; then
      echo "FAIL: no ssh from $VM after $tries attempts (~$((tries * 2))s)" >&2
      exit 1
    fi
    if [ "$i" -le 3 ] || [ $((i % 5)) -eq 0 ]; then
      echo "== waiting for ssh on $VM (attempt $i)"
    fi
    sleep 2
  done
  [ "$i" -gt 0 ] && echo "== ssh up on $VM after ~$((i * 2))s"
  return 0
}
wait_for_ssh

# Assert that the binary in the guest is the flavour the profile promises.
# `ldd` is the witness: the GNOME shell is configured with no KF6 in the
# link line at all, so a KF6-dependent binary on a gnome guest means the wrong
# package was installed, or packaging put the KDE tree into both.
verify_flavour() {
  local kf6
  kf6=$(Q "ldd /usr/bin/traduku 2>/dev/null | grep -c KF6" || true)
  kf6=$(echo "$kf6" | tr -dc "0-9")
  if [ "$FLAVOUR" = gnome ]; then
    [ "${kf6:-0}" = 0 ] || { echo "FAIL: $VM is a GNOME profile but /usr/bin/traduku links KF6 ($kf6 libs)" >&2; exit 1; }
    Q "test -f /usr/lib/udev/rules.d/70-traduku-uaccess.rules" \
      || { echo "FAIL: traduku-gnome shipped no uaccess rule - the /dev/input gesture gets no ACL without it" >&2; exit 1; }
    Q "command -v wl-paste" \
      || { echo "FAIL: traduku-gnome installed with no wl-clipboard (hard Depends: Wayland clipboard has no Qt fallback)" >&2; exit 1; }
  else
    [ "${kf6:-0}" -gt 0 ] || { echo "FAIL: $VM is a Plasma profile but /usr/bin/traduku links no KF6 - that is the GNOME flavour" >&2; exit 1; }
  fi
  echo "== flavour ok: $FLAVOUR"
}

case "$VM" in
  arch-*)
    # Two fences, and they used to disagree. mkrelease.sh packed HEAD while
    # the .deb container and the host suite build the working tree: an
    # uncommitted fix got a green grid against a package that did not contain
    # it. So what reaches makepkg is the working tree (--allow-dirty), and it
    # is reused only when its CONTENT is what built the cached package - tar
    # rewrites every mtime, so "is anything newer than the package" is not a
    # question with an answer here.
    FP=$( { git -C "$ROOT" ls-files -co --exclude-standard -z \
            | xargs -0 -r sha256sum; } | sha256sum | cut -d' ' -f1 )
    ART=$ROOT/.cache/vm-arch
    # `|| true` is load-bearing: pipefail makes a find on a directory that
    # does not exist yet kill the script, and "no cache yet" is the first run.
    CACHED=$(find "$ART" -maxdepth 1 -name 'traduku*.pkg.tar.zst' 2>/dev/null | head -1 || true)

    # The suite runs on every invocation, not "when a file looks newer": a
    # conditional gate is how a change that left mtimes alone skipped the only
    # thing that could have caught it. It costs seconds when nothing changed.
    echo "== host suite (gate before any package is built)"
    QT_QPA_PLATFORM=offscreen ctest --test-dir "$ROOT/build" --output-on-failure >/dev/null 2>&1 || {
      cmake -B "$ROOT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build "$ROOT/build" >/dev/null
      QT_QPA_PLATFORM=offscreen ctest --test-dir "$ROOT/build" --output-on-failure
    }

    T_PKG=0; t0=$SECONDS
    # Taken now, before anything is built: the check below is "did THIS run
    # produce a package", and a stamp made afterwards would accept last week's
    # tarball as this run's output.
    STAMP=$(mktemp)
    if [ "$REBUILD" != --rebuild ] && [ -n "$CACHED" ] \
       && [ "$(cat "$ART/.fingerprint" 2>/dev/null)" = "$FP" ]; then
      echo "== arch cache hit ($FP): reusing $(basename "$CACHED")"
      rm -rf "$ROOT/release-out"; mkdir -p "$ROOT/release-out"
      cp "$ART"/* "$ROOT/release-out"/
    else
      "$ROOT/packaging/arch/mkrelease.sh" "$ROOT/release-out" --allow-dirty --nocheck
      # A build that dies quietly is worse than a slow one: without this check
      # a red makepkg left the guest on whatever package it already had, and
      # every gate below passed while testing code nobody built.
      NEW=$(find "$ROOT/release-out" -name "traduku*.pkg.tar.zst" -newer "$STAMP" | head -1)
      [ -n "$NEW" ] || { echo "FAIL: mkrelease produced no package in this run (build is red above), refusing to test a stale one" >&2; rm -f "$STAMP"; exit 1; }
      T_PKG=$((SECONDS-t0))
      mkdir -p "$ART"; rm -f "$ART"/*
      cp "$ROOT/release-out"/traduku* "$ART"/ 2>/dev/null || true
      echo "$FP" > "$ART/.fingerprint.$PKGNAME"
    fi
    rm -f "$STAMP"
    echo "== push + pacman -U on $VM"
    # QEMU user-net needs a resolver (baked images predate the resolved patch)
    Q "grep -q DNS /etc/systemd/network/99-dhcp.network 2>/dev/null || echo vm | sudo -S sh -c '
        printf "[Match]\nName=eno* enp* eth*\n[Network]\nDHCP=yes\nDNS=10.0.2.3\n" > /etc/systemd/network/99-dhcp.network
        systemctl enable systemd-resolved; systemctl restart systemd-networkd systemd-resolved' || true"
    # trust the dev-channel signing key (once); real releases are signed
    SCP "$ROOT/packaging/arch/traduku-dev-pub.asc" vm@127.0.0.1:~/
    SCP "$HERE/vm-trust-key.sh" vm@127.0.0.1:~/
    Q "bash ~/vm-trust-key.sh"
    Q "rm -f ~/traduku-*.pkg.tar.zst*"
    # push the release artifacts exactly as a user would download them
    for art in "$PKG" "$PKG.sig"; do
      [ -f "$art" ] && SCP "$art" vm@127.0.0.1:~/
    done
    # rc was once swallowed by '| tail -3': a lock-blocked pacman left the
    # VM uninstalled and the suite ran 8-red on a phantom product.
    Q "echo vm | sudo -S pacman -U --noconfirm ~/${PKGNAME}-${VER}-1-x86_64.pkg.tar.zst >/tmp/pacman-install.log 2>&1 || true; tail -3 /tmp/pacman-install.log; pacman -Q ${PKGNAME} >/dev/null && pacman -Qi ${PKGNAME} | grep -E \"^(Version|Name)\" || { echo "FAIL: ${PKGNAME} is not installed on $VM" >&2; exit 1; }"
    verify_installed "$PKG"
    verify_flavour
    ;;
  *-kde|*-gnome)
    # .deb built inside the traduku-deb-builder container (dpkg toolchain
    # + Qt6/KF6 deps baked by tools/vms/deb-builder/Dockerfile). Baked
    # ONCE: an in-run apt-get hung on a trixie mirror stall once, and
    # rebuilds must not depend on the network. timeout is the last-resort
    # ceiling for any future container-side wedge.
    echo "== building .deb in the traduku-deb-builder container"
    IMG=traduku-deb-builder:trixie
    docker image inspect "$IMG" >/dev/null 2>&1 || \
      docker build -t "$IMG" "$HERE/deb-builder"
    # ---- is the .deb that is already there still the right one? ----------
    # The build costs minutes, the guest install seconds, and the suite gets
    # run over and over against an unchanged tree - repeating the build is the
    # whole wait. The Arch branch asks with `find -newer`; for the .deb that
    # answer is wrong in both directions: `git checkout` of an older commit
    # refreshes mtimes (a rebuild of a byte-identical tree), and an editor or
    # a `git stash pop` that preserves timestamps hides a real change. Hash
    # the CONTENTS instead: tracked files plus untracked-but-not-ignored ones,
    # which is what the container copies into /work.
    # Not under release-out: packaging/arch/mkrelease.sh rm -rf's that whole
    # directory, and a cache a sibling build can delete is not a cache.
    ART=$ROOT/.cache/vm-deb
    FP=$( { git -C "$ROOT" ls-files -co --exclude-standard -z \
            | xargs -0 -r sha256sum; } | sha256sum | cut -d' ' -f1 )
    T_BUILD=0
    t0=$SECONDS
    if [ "$REBUILD" != --rebuild ] && compgen -G "$ART/${PKGNAME}_*.deb" >/dev/null \
        && [ "$(cat "$ART/.fingerprint.$PKGNAME" 2>/dev/null)" = "$FP" ]; then
      echo "== .deb cache hit ($FP): the tree is identical to the one that built $PKGNAME"
      echo "   (force one with: vm-install.sh $VM --rebuild)"
    else
    STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
    timeout 1200 docker run --rm -v "$ROOT:/src:ro" -v "$STAGE:/out" "$IMG" bash -c '
      set -e
      cp -a /src /work; cd /work
      cp -r packaging/debian debian
      # nocheck: debhelper cmake would configure -DBUILD_TESTING=OFF and
      # run nothing; the suite therefore runs as the explicit gate below
      # (old-Qt corner of the matrix: Qt 6.8.2 of debian 13). No exclusions,
      # no repeats - the two old-Qt casualties are fixed in-tree: header-
      # CASE asserts (neighbor 7f82696) and the chunkedtranslator commit-
      # order race (neighbor 7d37663). A red here is a real regression.
      # NOTE: this block lives inside bash -c single quotes: no apostrophes.
      # First attempt verbose: when a build takes minutes, silence is not a
      # progress bar - the [n/m] lines from ninja are what a waiting developer reads.
      # The retry stays quiet, its purpose is the transient failure, not a log.
      DEB_BUILD_OPTIONS=nocheck dpkg-buildpackage -us -uc -b 2>&1 \
        || DEB_BUILD_OPTIONS=nocheck dpkg-buildpackage -us -uc -b >/dev/null 2>&1
      # The suite is the gate, run here where the toolchain lives - the
      # guest only ever installs. Both shells: the GNOME one has tests the
      # KDE tree never compiles (evdev gesture, FdoNotifier, single instance).
      # Run in the trees dpkg-buildpackage just built, not in a fresh pair of
      # gate-* directories: four full builds made the gate the most expensive
      # part of the VM cycle, and a second tree can pass while the packaged
      # one differs from it. Same tree, same binaries, same ctest.
      for tree in obj-*-kde obj-*-gnome; do
        if [ ! -x "$tree/tests/tst_appcontroller" ]; then
          echo "GATE: $tree carries no test binaries (dh built without BUILD_TESTING) - configuring it alone" >&2
          cmake -B gate-$tree -G Ninja -DCMAKE_BUILD_TYPE=Release -DLT_SHELL=${tree##*-} >/dev/null
          cmake --build gate-$tree >/dev/null
          tree=gate-$tree
        fi
        ( cd $tree && QT_QPA_PLATFORM=offscreen ctest --output-on-failure ) \
          || { echo "GATE RED: $tree suite" >&2; exit 1; }
      done
      cp /traduku*.deb /out/'
      mkdir -p "$ART"; rm -f "$ART"/*.deb
      cp "$STAGE"/traduku*.deb "$ART"/
      # ONE fingerprint per flavour. There were two packages and one stamp, so
      # building debian-kde recorded the tree as built, and the very next
      # debian-gnome run found its own (older) traduku-gnome .deb in the cache,
      # saw a fingerprint that another flavour had just written, and called it a
      # hit. The guest kept a binary from before --autostart and the suite
      # reported an installed, tested product.
      echo "$FP" > "$ART/.fingerprint.$PKGNAME"
    fi
    T_BUILD=$((SECONDS-t0))
    if [ "$FLAVOUR" = gnome ]; then
      DEB=$(ls "$ART"/traduku-gnome_*.deb | head -1)
    else
      # the underscore is load-bearing: traduku*.deb also matches the
      # traduku-gnome artefact and would install the other shell.
      DEB=$(ls "$ART"/traduku_*.deb | head -1)
    fi
    [ -f "$DEB" ] || { echo "FAIL: no $FLAVOUR .deb produced (got: $(ls "$ART"))" >&2; exit 1; }
    echo "== push + apt install on $VM ($DEB)"
    T_INSTALL=0; t0=$SECONDS
    Q "rm -f ~/traduku*.deb"
    SCP "$DEB" vm@127.0.0.1:~/
    # unattended-upgrades grabs the dpkg lock right after test_03's reboot;
    # an install hitting it used to die silently behind '| tail -3' (the
    # all-red suite after rebuild). Stop the timer, wait for the lock, and
    # VERIFY the package state afterwards - install is a gate, not a hint.
    Q "echo vm | sudo -S sh -c 'systemctl stop unattended-upgrades apt-daily.service apt-daily-upgrade.timer apt-daily-upgrade.service 2>/dev/null || true; i=0; while fuser /var/lib/dpkg/lock-frontend >/dev/null 2>&1 && [ \$i -lt 60 ]; do sleep 2; i=\$((i+1)); done; DEBIAN_FRONTEND=noninteractive apt-get install -y --reinstall /home/vm/${PKGNAME}_*.deb >/tmp/apt-install.log 2>&1' ; tail -3 /tmp/apt-install.log; grep -q '^Setting up ${PKGNAME}' /tmp/apt-install.log && dpkg -l ${PKGNAME} | grep -aq '^ii.*${PKGNAME}'"
    verify_installed "$DEB"
    verify_flavour
    T_INSTALL=$((SECONDS-t0))
    ;;
esac

echo "== gate: selftest on $VM"
Q "QT_QPA_PLATFORM=offscreen traduku --selftest 2>&1 | tail -3"
Q "QT_QPA_PLATFORM=offscreen traduku --help 2>&1 | head -3"
# Only the numbers this branch actually measured: printing n/a for the .deb
# cache on the Arch branch read like a broken cache and sent someone looking
# for a bug that was not there.
if [ -n "${T_BUILD+x}" ]; then
  echo "== timing: deb build ${T_BUILD}s (0 = cache hit), guest install ${T_INSTALL}s, total $((SECONDS-T0))s"
else
  # Only the numbers this branch has. "n/as" read like a measurement that
  # went wrong rather than a step this path does not take.
  out="== timing: package build+gates ${T_PKG}s"
  [ -n "${T_INSTALL+x}" ] && out="$out, guest install ${T_INSTALL}s"
  echo "$out, total $((SECONDS-T0))s"
fi
[ "$REBUILD" = --shell ] && $SSH -t vm@127.0.0.1 bash
exit 0
