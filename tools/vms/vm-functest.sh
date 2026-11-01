#!/usr/bin/env bash
# Functional gate on a grid VM: virtual keyboard (QEMU QMP input-send-event)
# + screenshots (QMP screendump: the guest scanout, so it is compositor
# independent and works on native Wayland too) + clipboard round-trip.
# usage: vm-functest.sh <vm>
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
VM=$1
IDX=$(cd "$HERE" && ./vmctl.sh status 2>/dev/null | awk -v v="$VM" '$1==v{print $3}' | tr -d 'idx=')
SSH="ssh -i $HOME/vms/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ServerAliveInterval=15 -o ServerAliveCountMax=3 -p 220${IDX:-1}"
Q() { $SSH vm@127.0.0.1 "$@" 2>/dev/null; }
fail=0; ok() { echo "  ok: $1"; }; bad() { echo "  FAIL: $1"; fail=1; }
case "$VM" in
  *-gnome) DE=gnome;;
  *-kde)   DE=kde;;
esac
case "$VM" in
  debian-*) MODE=x11;;
  ubuntu-kde) MODE=kwayland;;
  ubuntu-gnome) MODE=gwayland;;
esac
echo "== $VM (mode=$MODE, de=$DE)"
SHOTS="$HOME/vms/run/$VM/shots"
mkdir -p "$SHOTS"

# QMP framebuffer capture: no in-guest tool, no XGetImage on rootless
# Xwayland, works while the compositor is Wayland-native.
shot() {
  rm -f "$SHOTS/$1.ppm"
  "$HERE/vmctl.sh" shot "$VM" "$1" >/dev/null 2>&1
  [ -s "$SHOTS/$1.ppm" ]
}

# ---- VM-side helper script (no ssh quoting hell) --------------------------
Q 'cat > /tmp/ft.sh' <<'EOS'
#!/bin/bash
MODE=__MODE__
DE=__DE__
xwa() {  # mutter Xwayland auth (Ubuntu 25.10: /run/user dir)
  ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1
}
case "$1" in
  sessionenv)
    P=$(pgrep -u vm -x gnome-shell | head -1)
    [ -z "$P" ] && P=$(pgrep -u vm -x kwin_x11 | head -1)
    [ -z "$P" ] && P=$(pgrep -u vm -x kwin_wayland | head -1)
    echo vm | sudo -S cat /proc/$P/environ | tr '\0' '\n' \
      | grep -E "^(DISPLAY|WAYLAND_DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_RUNTIME_DIR|XDG_SESSION_TYPE)=" \
      | sed 's/^/export /' > /tmp/session.env
    grep -q WAYLAND_DISPLAY /tmp/session.env || echo "export WAYLAND_DISPLAY=wayland-0" >> /tmp/session.env
    grep -q XDG_RUNTIME_DIR /tmp/session.env || echo "export XDG_RUNTIME_DIR=/run/user/1000" >> /tmp/session.env;;
  prepare)  # idle screen locking kills the gate: unlock now, never re-lock
    echo vm | sudo -S loginctl unlock-sessions 2>/dev/null
    source /tmp/session.env
    if [ "$DE" = gnome ]; then
      gsettings set org.gnome.desktop.screensaver lock-enabled false 2>/dev/null
      gsettings set org.gnome.desktop.screensaver idle-activation-enabled false 2>/dev/null
      gsettings set org.gnome.desktop.session idle-delay 0 2>/dev/null
      # the "About GNOME" welcome dialog grabs focus and swallows the gesture
      gsettings set org.gnome.shell welcome-dialog-last-shown-version \
        "$(gnome-shell --version 2>/dev/null | awk '{print $3}')" 2>/dev/null
      pkill -f gnome-welcome 2>/dev/null
    else
      kwriteconfig6 --file kscreenlockerrc --group Daemon --key Autolock false 2>/dev/null
      kwriteconfig6 --file kscreenlockerrc --group Daemon --key LockOnResume false 2>/dev/null
      [ -n "$DISPLAY" ] && DISPLAY=$DISPLAY xset s off -dpms 2>/dev/null
    fi;;
  mock)
    pkill -f "[m]ock-openai.py" 2>/dev/null; sleep 0.5
    nohup python3 ~/build/traduku-0.2.0/tools/mock-openai.py 8931 >/tmp/mock.log 2>&1 &
    sleep 1; curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8931/v1/models;;
  config)
    mkdir -p ~/.config/traduku
    cat > ~/.config/traduku/config.ini <<'INI'
[General]
ApiUrl=http://127.0.0.1:8931/v1
Model=mock
TargetLanguage=en
PressInterval=0.8
SoundsEnabled=false
CopyResult=true
FirstRunDone=true
INI
    grep -c ApiUrl ~/.config/traduku/config.ini;;
  cli)
    source /tmp/session.env
    if [ "$MODE" = gwayland ]; then export DISPLAY=:0 XAUTHORITY=$(xwa); QT_QPA_PLATFORM=xcb; fi
    timeout 30 traduku --translate --text "Hello grid" 2>/dev/null | tr -d '\n';;
  launch)
    pkill -x traduku 2>/dev/null; sleep 0.5
    source /tmp/session.env
    if [ "$MODE" = gwayland ]; then DISPLAY=:0 XAUTHORITY=$(xwa) QT_QPA_PLATFORM=xcb setsid nohup traduku >/tmp/traduku.log 2>&1 &
    else setsid nohup traduku >/tmp/traduku.log 2>&1 & fi
    sleep 4; pgrep -x traduku | head -1;;
  clipset)
    source /tmp/session.env
    [ "$MODE" = gwayland ] && { export DISPLAY=:0 XAUTHORITY=$(xwa); pkill -x xclip; sleep 0.5; }
    case "$MODE" in
      x11) setsid sh -c 'printf "Hello grid" | xclip -selection clipboard' >/dev/null 2>&1 &;;
      kwayland) printf 'Hello grid' | timeout 10 wl-copy >/dev/null 2>&1;;
      gwayland) setsid sh -c 'DISPLAY=:0 XAUTHORITY='"$(xwa)"' sh -c "printf \"Hello grid\" | xclip -selection clipboard"' >/dev/null 2>&1 &;;
    esac
    sleep 1;;
  clipget)
    source /tmp/session.env
    [ "$MODE" = gwayland ] && export DISPLAY=:0 XAUTHORITY=$(xwa)
    case "$MODE" in
      x11) timeout 10 xclip -selection clipboard -o;;
      kwayland) timeout 10 wl-paste 2>/dev/null;;
      gwayland) timeout 10 xclip -selection clipboard -o;;
    esac;;
  xterm)  # focus carrier for gnome-wayland (keys must land on an X window)
    source /tmp/session.env
    DISPLAY=:0 XAUTHORITY=$(xwa) setsid nohup xterm -e sh -c 'trap "" INT; exec sleep 300' >/dev/null 2>&1 & sleep 2; pgrep -x xterm | head -1;;
  win)  # count mapped "Traduku" windows (the app is an Xwayland client here)
    source /tmp/session.env
    DISPLAY=:0 XAUTHORITY=$(xwa) xdotool search --onlyvisible --name '^Traduku$' 2>/dev/null | wc -l;;
  log)
    grep -E "listener|RECORD|translat" /tmp/traduku.log | head -5;;
  stop)
    pkill -x traduku; pkill -x xterm; pkill -f "[m]ock-openai.py";;
esac
EOS
Q "sed -i 's/__MODE__/$MODE/; s/__DE__/$DE/' /tmp/ft.sh"

Q 'bash /tmp/ft.sh sessionenv'
Q 'bash /tmp/ft.sh prepare'
Q 'bash /tmp/ft.sh mock' | grep -q 200 && ok "mock-openai up" || bad "mock-openai"
Q 'bash /tmp/ft.sh config' | grep -q 1 && ok "config.ini" || bad "config.ini"

OUT=$(Q 'bash /tmp/ft.sh cli')
[ -n "$OUT" ] && echo "$OUT" | grep -q "Hello grid" && ok "CLI --translate round-trip" || bad "CLI --translate got: '$OUT'"

Q 'bash /tmp/ft.sh launch' >/dev/null
Q 'pgrep -x traduku' >/dev/null && ok "app running" || { bad "app not running"; Q 'tail -5 /tmp/traduku.log'; }

Q 'bash /tmp/ft.sh clipset'
[ "$MODE" = gwayland ] && Q 'bash /tmp/ft.sh xterm' >/dev/null
Q 'bash /tmp/ft.sh clipget' | grep -q "Hello grid" && ok "clipboard seeded" || bad "clipboard seed"

shot pre || bad "pre screenshot"
if [ "$MODE" = kwayland ]; then SEQ="ctrl alt c;200;ctrl alt c"; else SEQ="ctrl c;200;ctrl c"; fi
"$HERE/vmctl.sh" keyseq "$VM" "$SEQ" >/dev/null 2>&1

# Poll instead of a fixed sleep: the translation is streamed by the mock.
GOT=""
for _ in $(seq 1 25); do
  sleep 1
  GOT=$(Q 'bash /tmp/ft.sh clipget')
  case "$GOT" in *"MOCK -> en"*) break;; esac
done
shot post || bad "post screenshot"

echo "$GOT" | grep -qi "MOCK -> en" && ok "gesture -> translate -> clipboard" || bad "gesture pipeline (clipboard: $(echo "$GOT" | head -c 60); log: $(Q 'bash /tmp/ft.sh log' | tr '\n' ' '))"

docker exec traduku-vmm python3 - "$VM" <<'PYDIFF'
import sys
from PIL import Image, ImageChops
d=f"/vms/run/{sys.argv[1]}/shots"
a=Image.open(f"{d}/pre.ppm").convert("RGB"); b=Image.open(f"{d}/post.ppm").convert("RGB")
b.convert("RGB").save(f"{d}/post.png"); a.save(f"{d}/pre.png")
bbox=ImageChops.difference(a,b).getbbox()
area=(bbox[2]-bbox[0])*(bbox[3]-bbox[1]) if bbox else 0
frac=area/(a.size[0]*a.size[1])
print(f"changed-area frac: {frac:.3f}")
sys.exit(0 if frac>0.05 else 1)
PYDIFF
[ $? -eq 0 ] && ok "window appeared (screenshot diff)" || bad "no visible window after gesture"

if [ "$MODE" = gwayland ]; then
  W=$(Q 'bash /tmp/ft.sh win')
  [ "${W:-0}" -gt 0 ] 2>/dev/null && ok "mapped Traduku window ($W)" || bad "no mapped Traduku window"
fi

Q 'bash /tmp/ft.sh stop' >/dev/null
exit $fail
