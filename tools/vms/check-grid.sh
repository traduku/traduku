#!/usr/bin/env bash
# Grid readiness gate. usage: check-grid.sh <vm>
# Asserts: ssh up, cloud-init done, deps installed, X11 session processes running.
set -uo pipefail
VM=$1
DIR=$(cd "$(dirname "$0")" && pwd)
S="$DIR/vmctl.sh ssh $VM"
fail=0
chk() { local d=$1; shift; if $S "$@" >/dev/null 2>&1; then echo "  ok: $d"; else echo "  FAIL: $d"; fail=1; fi; }
echo "== $VM"
for i in $(seq 1 30); do $S true 2>/dev/null && break; sleep 10; done
chk "ssh reachable" true
chk "cloud-init done" "cloud-init status | grep -q done"
chk "build deps (cmake/ninja/debhelper)" "which cmake ninja dpkg-buildpackage"
chk "Qt6 dev" "test -d /usr/lib/x86_64-linux-gnu/cmake/Qt6 -o -d /usr/lib/cmake/Qt6"
chk "KF6 dev" "ls /usr/lib/x86_64-linux-gnu/cmake/KF6StatusNotifierItem /usr/lib/cmake/KF6StatusNotifierItem 2>/dev/null | grep -q ."
chk "graphical session active on seat0" "loginctl list-sessions --no-legend | grep seat0 | grep -q 'tty[12]'"
chk "DE shell running" "pgrep -x gnome-shell || pgrep -x kwin_wayland || pgrep -x kwin_x11"
chk "autologined user=vm" "who | grep -q 'vm '"
exit $fail
