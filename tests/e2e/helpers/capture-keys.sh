#!/bin/bash
# Focus carrier + typed-text capture: xterm reads one line, copies to clipboard.
# usage: capture-keys.sh &  (then focus it from the test, type, Enter)
export XDG_RUNTIME_DIR=/run/user/1000
source /tmp/session.env
setsid xterm -title capturer -e sh -c 'read x; printf %s "$x" | wl-copy >/dev/null 2>&1' >/dev/null 2>&1 &
sleep 1.5
xdotool search -name capturer | head -1
