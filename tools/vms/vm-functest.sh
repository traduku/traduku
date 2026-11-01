#!/usr/bin/env bash
# Functional gate on a grid VM: virtual keyboard (QEMU QMP sendkey) +
# screenshots (QMP screendump) + clipboard round-trip assertions.
# usage: vm-functest.sh <vm>
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
VM=$1
IDX=$(cd "$HERE" && ./vmctl.sh status 2>/dev/null | awk -v v="$VM" '$1==v{print $3}' | tr -d 'idx=')
SSH="ssh -i $HOME/vms/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 220${IDX:-1}"
Q() { $SSH vm@127.0.0.1 "$@" 2>/dev/null; }
fail=0; ok() { echo "  ok: $1"; }; bad() { echo "  FAIL: $1"; fail=1; }
case "$VM" in
  debian-*) MODE=x11;;
  ubuntu-kde) MODE=kwayland;;
  ubuntu-gnome) MODE=gwayland;;
esac
echo "== $VM (mode=$MODE)"

# ---- VM-side helper script (no ssh quoting hell) --------------------------
Q 'cat > /tmp/ft.sh' <<'EOS'
#!/bin/bash
source /tmp/session.env
MODE=__MODE__
xwa() {  # XWayland auth path for gnome-shell's on-demand Xwayland
  tr '\0' ' ' < /proc/$(pgrep -u vm -x Xwayland | head -1)/cmdline \
    | awk '{for(i=1;i<=NF;i++) if($i=="-auth") print $(i+1)}'
}
case "$1" in
  sessionenv)
    P=$(pgrep -u vm -x gnome-shell | head -1)
    [ -z "$P" ] && P=$(pgrep -u vm -x kwin_x11 | head -1)
    [ -z "$P" ] && P=$(pgrep -u vm -x kwin_wayland | head -1)
    echo vm | sudo -S cat /proc/$P/environ | tr '\0' '\n' \
      | grep -E "^(DISPLAY|WAYLAND_DISPLAY|XAUTHORITY|DBUS_SESSION_BUS_ADDRESS|XDG_RUNTIME_DIR|XDG_SESSION_TYPE)=" \
      | sed 's/^/export /' > /tmp/session.env;;
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
INI
    grep -c ApiUrl ~/.config/traduku/config.ini;;
  cli)
    traduku --translate --text "Hello grid" 2>/dev/null | tr -d '\n';;
  launch)
    pkill -x traduku 2>/dev/null; sleep 0.5
    if [ "$MODE" = gwayland ]; then QT_QPA_PLATFORM=xcb setsid nohup traduku >/tmp/traduku.log 2>&1 &
    else setsid nohup traduku >/tmp/traduku.log 2>&1 & fi
    sleep 4; pgrep -x traduku | head -1;;
  clipset)
    case "$MODE" in
      x11) printf 'Hello grid' | xclip -selection clipboard;;
      kwayland) printf 'Hello grid' | wl-copy;;
      gwayland) DISPLAY=:1 XAUTHORITY=$(xwa) sh -c 'printf "Hello grid" | xclip -selection clipboard';;
    esac;;
  clipget)
    case "$MODE" in
      x11) xclip -selection clipboard -o;;
      kwayland) wl-paste;;
      gwayland) DISPLAY=:1 XAUTHORITY=$(xwa) xclip -selection clipboard -o;;
    esac;;
  xterm)  # focus carrier for gnome-wayland (keys must land on an X window)
    DISPLAY=:1 XAUTHORITY=$(xwa) setsid nohup xterm -e sleep 120 >/dev/null 2>&1 & sleep 2; pgrep -x xterm | head -1;;
  stop)
    pkill -x traduku; pkill -x xterm; pkill -f "[m]ock-openai.py";;
esac
EOS
Q "sed -i 's/__MODE__/$MODE/' /tmp/ft.sh"

Q 'bash /tmp/ft.sh sessionenv'
Q 'bash /tmp/ft.sh mock' | grep -q 200 && ok "mock-openai up" || bad "mock-openai"
Q 'bash /tmp/ft.sh config' | grep -q 1 && ok "config.ini" || bad "config.ini"

OUT=$(Q 'bash /tmp/ft.sh cli')
[ "$OUT" = "HELLO GRID" ] && ok "CLI --translate round-trip" || bad "CLI --translate got: '$OUT'"

Q 'bash /tmp/ft.sh launch' >/dev/null
Q 'pgrep -x traduku' >/dev/null && ok "app running" || { bad "app not running"; Q 'tail -5 /tmp/traduku.log'; }

Q 'bash /tmp/ft.sh clipset'
Q 'bash /tmp/ft.sh clipget' | grep -q "Hello grid" && ok "clipboard seeded" || bad "clipboard seed"

"$HERE/vmlife.sh" shot "$VM" pre >/dev/null 2>&1
[ "$MODE" = gwayland ] && Q 'bash /tmp/ft.sh xterm' >/dev/null
if [ "$MODE" = kwayland ]; then SEQ="ctrl alt c;200;ctrl alt c"; else SEQ="ctrl c;200;ctrl c"; fi
"$HERE/vmctl.sh" key "$VM" "$SEQ" >/dev/null 2>&1
sleep 6
"$HERE/vmlife.sh" shot "$VM" post >/dev/null 2>&1

Q 'bash /tmp/ft.sh clipget' | grep -qi "HELLO GRID" && ok "gesture -> translate -> clipboard" || bad "gesture pipeline (clipboard: $(Q 'bash /tmp/ft.sh clipget' | head -c 60))"

docker exec traduku-vmm python3 - "$VM" <<'PY'
import sys
from PIL import Image, ImageChops
d=f"/vms/run/{sys.argv[1]}/shots"
a=Image.open(f"{d}/pre.png").convert("RGB"); b=Image.open(f"{d}/post.png").convert("RGB")
bbox=ImageChops.difference(a,b).getbbox()
area=(bbox[2]-bbox[0])*(bbox[3]-bbox[1]) if bbox else 0
print(f"changed-area frac: {area/(a.size[0]*a.size[1]):.3f}")
sys.exit(0 if area/(a.size[0]*a.size[1])>0.05 else 1)
PY
[ $? -eq 0 ] && ok "window appeared (screenshot diff)" || bad "no visible window after gesture"

Q 'bash /tmp/ft.sh stop' >/dev/null
exit $fail
