#!/usr/bin/env bash
# VM lifecycle + QMP helpers. Runs in the container.
# usage: vmlife.sh {start|stop|status|logs|shot|key|keyseq} [vm] [args]
set -euo pipefail
VMS_HOME=${VMS_HOME:-/vms}
VMS=(debian-kde debian-gnome ubuntu-kde ubuntu-gnome)
cmd=${1:-status}; VM=${2:-}

idx_of() { local i=1; for v in "${VMS[@]}"; do [ "$v" = "$1" ] && { echo $i; return; }; i=$((i+1)); done; echo 0; }

case "$cmd" in
  status)
    for v in "${VMS[@]}"; do
      d=$VMS_HOME/run/$v; s=no-disk
      [ -f "$d/disk.qcow2" ] && s=stopped
      p=0
      [ -f "$d/qemu.pid" ] && kill -0 "$(cat "$d/qemu.pid")" 2>/dev/null && { s=running; p=$(cat "$d/qemu.pid"); }
      printf '%-12s %-8s idx=%s ssh=:%s vnc=:%s pid=%s\n' "$v" "$s" "$(idx_of "$v")" "$((2200+$(idx_of "$v")))" "$((5900+$(idx_of "$v")))" "$p"
    done;;
  start)
    d=$VMS_HOME/run/$VM; [ -d "$d" ] || { echo "no vm $VM" >&2; exit 1; }
    [ -f "$d/qemu.pid" ] && kill -0 "$(cat "$d/qemu.pid")" 2>/dev/null && { echo "already running"; exit 0; }
    i=$(idx_of "$VM")
    qemu-system-x86_64 -enable-kvm -m 4096 -smp 4 -cpu max \
      -drive if=virtio,file="$d/disk.qcow2" \
      -cdrom "$d/seed.iso" \
      -netdev "user,id=n0,hostfwd=tcp::220$i-:22" -device virtio-net-pci,netdev=n0 \
      -device virtio-gpu-pci -display none -vnc ":$i" \
      -serial "file:$d/serial.log" \
      -qmp "unix:$d/qmp.sock,server,nowait" \
      -pidfile "$d/qemu.pid" -daemonize
    echo "$VM started (ssh :$((2200+i)) vnc :590$i)";;
  stop)
    d=$VMS_HOME/run/$VM
    if [ -f "$d/qemu.pid" ] && kill -0 "$(cat "$d/qemu.pid")" 2>/dev/null; then
      python3 /tools/qmp.py "$d/qmp.sock" system_powerdown && sleep 8
      kill -0 "$(cat "$d/qemu.pid")" 2>/dev/null && { echo "force kill"; kill "$(cat "$d/qemu.pid")"; } || true
    fi
    rm -f "$d/qemu.pid"; echo "$VM stopped";;
  logs)
    tail -30 "$VMS_HOME/run/$VM/serial.log" 2>/dev/null || echo "no serial log (attach: socat)";;
  shot)
    # shot <vm> [name]  -> writes $d/shots/<name>.png (ppm converted via PIL)
    d=$VMS_HOME/run/$VM; n=${3:-$(date +%s)}
    mkdir -p "$d/shots"
    python3 /tools/qmp.py "$d/qmp.sock" screendump "$d/shots/$n.ppm"
    python3 - "$d/shots/$n.ppm" "$d/shots/$n.png" <<'PY'
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
print(sys.argv[2])
PY
    ;;
  key|keyseq)
    # key <vm> <qemu-key>... ; keyseq <vm> "ctrl c;150;ctrl c" (ms between)
    d=$VMS_HOME/run/$VM; shift 2
    python3 /tools/qmp.py "$d/qmp.sock" "$cmd" "$*";;
  *) echo "usage: vmlife.sh {start|stop|status|logs|shot|key|keyseq} [vm]" >&2; exit 2;;
esac
