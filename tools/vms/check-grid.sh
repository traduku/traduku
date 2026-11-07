#!/usr/bin/env bash
# Grid readiness gate. usage: check-grid.sh <vm>
#
# What "ready" means here changed when the grid stopped compiling. The guest is
# a *user's machine*: it must have a desktop and a package manager, not a
# compiler. Asserting cmake/ninja/qt6-dev in the guest (the old checks) tested
# the build host, hid a missing runtime Depends - and a package that forgot
# wl-clipboard still passed, because the guest had a toolchain by accident.
#
# The package contract itself (installed, on PATH, right flavour, right files)
# is asserted by tests/e2e/test_01_install.py after vm-install.sh pushes the
# artefacts; this script only answers "can this VM be used at all".
set -uo pipefail
VM=$1
DIR=$(cd "$(dirname "$0")" && pwd)
S="$DIR/vmctl.sh ssh $VM"
fail=0
chk() { local d=$1; shift; if $S "$@" >/dev/null 2>&1; then echo "  ok: $d"; else echo "  FAIL: $d"; fail=1; fi; }
echo "== $VM"
for i in $(seq 1 30); do $S true 2>/dev/null && break; sleep 10; done
chk "ssh reachable" true
# cloud-init only exists on the cloud-image distros; the Arch base is built by
# mkarchbase.sh with no cloud-init at all, so asserting it there was a check
# that could only ever be red - and a permanently red line is how a gate stops
# being read. On Arch the equivalent of "the guest finished booting" is that
# systemd is done and nothing failed.
case "$VM" in
  arch-*)
    # no --wait: it turns the check into a blocking job-wait, and a guest that
    # is still settling returned nothing at all (a red with no information).
    chk "systemd settled" "systemctl is-system-running | grep -EqE '^(run|degrad)'"
    chk "no failed units" "! systemctl --failed --no-legend --no-pager | grep -q ."
    ;;
  *)
    chk "cloud-init done" "cloud-init status | grep -q done"
    ;;
esac
chk "graphical session active on seat0" "loginctl list-sessions --no-legend | grep seat0 | grep -q 'tty[12]'"
chk "DE shell running" "pgrep -x gnome-shell || pgrep -x kwin_wayland || pgrep -x kwin_x11"
# `who` reads utmp, and a gdm/sddm autologin session does not necessarily
# write one - so `who` came back empty on a seat0 session that loginctl was
# perfectly happy to name. loginctl is the authority here anyway.
SEATUSER=$($S "loginctl list-sessions --no-legend 2>/dev/null | awk '\$4==\"seat0\"{print \$3}' | head -1" 2>/dev/null | tail -1)
chk "autologined user=vm" "test '$SEATUSER' = vm || echo 'seat0 session owner: $SEATUSER' >&2; test '$SEATUSER' = vm"

# The desktop the profile claims must be the desktop that is running: an
# ubuntu-gnome VM that came up on X11/KDE would run the wrong tray host and
# every tray test would then be measuring something else.
case "$VM" in
  *-gnome) chk "gnome-shell (the profile promises GNOME)" "pgrep -x gnome-shell" ;;
  *-kde)   chk "kwin (the profile promises Plasma)" "pgrep -x kwin_wayland || pgrep -x kwin_x11" ;;
esac
chk "package manager present" "command -v pacman || command -v apt-get"

# Informational, never fatal: the app may legitimately not be installed yet.
if $S "command -v traduku" >/dev/null 2>&1; then
  echo "  note: traduku already installed ($($S 'traduku --version' 2>/dev/null | tr -d '\n'))"
else
  echo "  note: traduku not installed yet (expected before vm-install.sh)"
fi
exit $fail
