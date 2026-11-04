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
  say "   no readable keyboard. Choose one (then re-run this script):"
  say "     sudo usermod -aG input \$USER      # permanent, needs re-login"
  say "     sudo setfacl -R -m u:\$USER:rw /dev/input/event*   # until next boot"
  say "   (this is the keylogger-grade permission the evdev gesture costs;"
  say "    GNOME_PLAN.md D1 — the fallback paths work without it)"
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
