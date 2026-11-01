#!/usr/bin/env bash
# VM lifecycle + QMP helpers. Runs in the container.
# usage: vmlife.sh {start|stop|status|logs|shot|key|keyseq} [vm] [args]
set -euo pipefail
VMS_HOME=${VMS_HOME:-/vms}
VMS=(arch-kde p-debian-kde)
cmd=${1:-status}; VM=${2:-}

idx_of() { local i=1; for v in "${VMS[@]}"; do [ "$v" = "$1" ] && { echo $((4+i)); return; }; i=$((i+1)); done; echo 0; }

case "$cmd" in
  status)
    for v in "${VMS[@]}"; do
      d=$VMS_HOME/run/$v; s=no-disk
      [ -f "$d/disk.qcow2" ] && s=stopped
      p=0
      [ -f "$d/qemu.pid" ] && kill -0 "$(cat "$d/qemu.pid")" 2>/dev/null && { s=running; p=$(cat "$d/qemu.pid"); }
      printf '%-14s %-8s idx=%s ssh=:%s vnc=:%s pid=%s\n' "$v" "$s" "$(idx_of "$v")" "$((2200+$(idx_of "$v")))" "$((5900+$(idx_of "$v")))" "$p"
    done;;
  start)
    d=$VMS_HOME/run/$VM; [ -d "$d" ] || { echo "no vm $VM" >&2; exit 1; }
    chmod a+rwX "$d" 2>/dev/null || true
    [ -f "$d/qemu.pid" ] && kill -0 "$(cat "$d/qemu.pid")" 2>/dev/null && { echo "already running"; exit 0; }
    i=$(idx_of "$VM")
    SEED=""
    [ -f "$d/seed.iso" ] && SEED="-cdrom $d/seed.iso"
    KERN=()
    # Arch base has no bootloader: QEMU boots the kernel exported by
    # mkarchbase.sh (arch-boot/), root by UUID baked into the image.
    if [ "$(cat "$d/distro" 2>/dev/null)" = arch ]; then
      KERN=(-kernel "$VMS_HOME/images/arch-boot/vmlinuz-linux"
            -initrd "$VMS_HOME/images/arch-boot/initramfs-linux.img"
            -append "root=UUID=$(cat "$VMS_HOME/images/arch-boot/root.uuid") rw console=ttyS0 loglevel=3 quiet")
    fi
    qemu-system-x86_64 -enable-kvm -m 4096 -smp 4 -cpu max \
      -drive if=virtio,file="$d/disk.qcow2" \
      $SEED "${KERN[@]}" \
      -netdev "user,id=n0,hostfwd=tcp::220$i-:22" -device virtio-net-pci,netdev=n0 \
      -vga none -device virtio-gpu-pci -display none -vnc ":$i" \
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
    tail -30 "$VMS_HOME/run/$VM/serial.log" 2>/dev/null || echo "no serial log";;
  shot)
    d=$VMS_HOME/run/$VM; n=${3:-$(date +%s)}
    mkdir -p "$d/shots"; chmod a+rwX "$d/shots"
    python3 /tools/qmp.py "$d/qmp.sock" screendump "$d/shots/$n.ppm"
    chmod a+rw "$d/shots/$n.ppm"
    chmod a+rw "$d/qmp.sock" 2>/dev/null || true
    ;;
  key|keyseq)
    # Virtual keyboard via QMP input-send-event (input layer -> ps2 kbd).
    d=$VMS_HOME/run/$VM
    python3 /tools/qmp.py "$d/qmp.sock" "$cmd" "${3:-}"
    ;;
  mouse)
    # Virtual mouse via QMP input-send-event (usb-tablet, absolute).
    d=$VMS_HOME/run/$VM
    python3 /tools/qmp.py "$d/qmp.sock" mouse "$@"
    ;;
  *) echo "commands: start stop status logs shot key keyseq mouse" >&2; exit 2;;
esac
