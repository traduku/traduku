#!/usr/bin/env bash
# The clipboard tests that need a real X server, run against one.
#
# Why a separate script: tst_x11clipboard skips when nothing is reachable at
# DISPLAY, because a unit suite that cannot open a display must not fail on a
# build daemon. That is the right call for a normal run and the wrong call for
# a gate - a skip is green, and the tests would have been green while testing
# nothing. Here the server is provided and TRADUKU_REQUIRE_X11=1 turns "no
# server" into a failure, so green means the negotiation was exercised.
#
# The container is the deb-builder image: it already carries Qt6, cmake and
# libx11-dev for the .deb gate, so this costs no second toolchain.
#
# usage: tools/vms/x11-ctest.sh [-v]
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
IMG=traduku-deb-builder:trixie
VERBOSE=${1:-}

# Always build, never "inspect then skip": that is how a gate ends up running
# an image that predates the Dockerfile - the container here had no Xvfb until
# the Dockerfile asked for one, and a cached image would have kept saying no.
# docker's layer cache makes the no-change case a few seconds.
docker build -q -t "$IMG" "$HERE/deb-builder" >/dev/null

docker run --rm -i \
  -v "$ROOT":/src -w /src \
  -e TRADUKU_REQUIRE_X11=1 -e DISPLAY=:99 -e QT_QPA_PLATFORM=offscreen \
  "$IMG" bash -euo pipefail <<EOS2
set -x
Xvfb :99 -screen 0 1024x768x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
for i in \$(seq 1 50); do [ -e /tmp/.X11-unix/X99 ] && break; sleep 0.2; done
[ -e /tmp/.X11-unix/X99 ] || { echo "Xvfb did not start:"; cat /tmp/xvfb.log; exit 1; }
echo "== X server up: \$(xdpyinfo | grep -m1 dimensions)"
cmake -S . -B build-x11 -G Ninja -DTRADUKU_BUILD_DESKTOP=KDE -DCMAKE_BUILD_TYPE=Debug \\
    -DTRADUKU_QT_PREFIX=/usr -DTRADUKU_KF6_PREFIX=/usr
cmake --build build-x11 -j\$(nproc)
ctest --test-dir build-x11 --output-on-failure ${VERBOSE:+-V}
EOS2
rc=$?
echo "x11-ctest rc=$rc"
exit $rc
