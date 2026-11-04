#!/usr/bin/env bash
# Stage 0 field test (GNOME_PLAN.md §9). Run it INSIDE the graphical session:
#
#   ./tools/spike/stage0.sh
#
# It answers the two questions the automated tests cannot: can the literal
# Ctrl+C+C be observed (kernel input layer), and which clipboard path
# survives being a background client. Output goes to /tmp/traduku-stage0.log
# as well, so it can be pasted into a report.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
BIN=${BIN:-build/bin}
LOG=/tmp/traduku-stage0.log
: > "$LOG"
say() { echo "$@" | tee -a "$LOG"; }

if [ -z "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ]; then
  say "run this from the graphical session (no WAYLAND_DISPLAY/DISPLAY)"
  exit 1
fi
for t in "$BIN/evdev-spike" "$BIN/clip-spike"; do
  [ -x "$t" ] || { say "missing $t - cmake --build build"; exit 1; }
done

say "== 1) keyboards in /dev/input ======================================"
"$BIN/evdev-spike" list | tee -a "$LOG"
ev_rc=${PIPESTATUS[0]}
if [ "$ev_rc" -ne 0 ]; then
  say ""
  say "   no readable keyboard. The right way is a udev uaccess rule"
  say "   (logind gives the node to the ACTIVE session only), NOT the"
  say "   'input' group, which grants it to every program of the account:"
  say "     sudo install -Dm644 packaging/udev/99-traduku-uaccess.rules \\"
  say "          /etc/udev/rules.d/99-traduku-uaccess.rules"
  say "     sudo udevadm control --reload"
  say "     sudo udevadm trigger --action=add --subsystem-match=input   # add!"
  say "     getfacl /dev/input/event3        # expect: user:\$USER:rw-"
  say "   (one-shot for testing only, until next boot:"
  say "     sudo setfacl -R -m u:\$USER:rw /dev/input/event*)"
  say "   The gesture has fallbacks without it (gsettings shortcut, tray)."
  say ""
fi

say "== 2) gesture: press Ctrl+C twice within 25 s ======================"
say "   (each Ctrl+C separately, Ctrl held; in any app — it must not"
say "    disturb normal copying)"
"$BIN/evdev-spike" listen 25 | tee -a "$LOG"

say "== 3) clipboard: copy something for real ==========================="
say "   select text in ANY app, press Ctrl+C once, then press Enter here."
read -r _
"$BIN/clip-spike" probe --xcb | tee -a "$LOG"

say ""
say "log: $LOG"
say "reading of the table: OK on C (wl-paste) or E (x11) = that backend can"
say "read the DE clipboard from the background; A/B are Qt's own wayland path."
