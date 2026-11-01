#!/usr/bin/env bash
# VM grid controller (runs on the HOST; talks to the traduku-pipeline-vmm
# container). Grid (PIPELINE.md): arch-kde  p-debian-kde
# Each VM: qcow2 (Arch: built from the official bootstrap by mkarchbase.sh;
# Debian: official cloud image) + NoCloud seed ISO (Debian only).
# Access: ssh 127.0.0.1:2205..2206 (vm/vm + ~/vms/id_ed25519), VNC :5905..,
# QMP sockets at ~/vms/run/<vm>/qmp.sock (screenshots + virtual keyboard).
# Port base 2204 keeps the sibling traduku-vmm grid (2201..2204) free.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
VMS_HOME="${VMS_HOME:-$HOME/vms}"
CTR=traduku-pipeline-vmm
VMS=(arch-kde p-debian-kde)

dexec() { docker exec "$CTR" bash -c "$*"; }

ensure_ctr() {
  docker ps --format '{{.Names}}' | grep -qx "$CTR" && return 0
  "$HERE/setup-ctr.sh"
}

idx_of() { local i=1; for v in "${VMS[@]}"; do [ "$v" = "$1" ] && { echo $((4+i)); return; }; i=$((i+1)); done; echo "unknown vm $1" >&2; return 1; }

cmd=${1:-status}; shift || true
case "$cmd" in
  up) ensure_ctr;;
  mkarchbase)
    ensure_ctr; dexec "VMS_HOME=/vms bash /tools/mkarchbase.sh";;
  mkvm)
    [ $# -eq 3 ] || { echo "usage: vmctl.sh mkvm <vm> <distro:arch|debian|ubuntu> <de:kde|gnome>" >&2; exit 2; }
    ensure_ctr; dexec "VMS_HOME=/vms bash /tools/mkvm.sh $*";;
  start|stop|logs|shot|key|keyseq|mouse)
    [ $# -ge 1 ] || { echo "usage: vmctl.sh $cmd <vm> [args]" >&2; exit 2; }
    ensure_ctr; dexec "VMS_HOME=/vms bash /tools/vmlife.sh $cmd $(printf '%q ' "$@")";;
  ssh)
    vm=$1; shift
    ssh -i "$VMS_HOME/id_ed25519" -p $((2200+$(idx_of "$vm"))) \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o ConnectTimeout=10 "vm@127.0.0.1" "$@";;
  vnc)
    # live view of the VM screen: the QEMU VNC server (:5900+idx) is on the
    # host network; the same display QMP screendump/keyseq drive, so the
    # e2e tests are fully visible (gestures, windows, notifications).
    vm=$1
    port=$((5900+$(idx_of "$vm")))
    for v in vncviewer tigervncviewer remmina vinagre; do
      command -v "$v" >/dev/null 2>&1 || continue
      case "$v" in
        remmina)   exec remmina -c "vnc://127.0.0.1:$port";;
        vinagre)   exec vinagre "vnc://127.0.0.1:$port";;
        *)         exec "$v" "127.0.0.1:$port";;
      esac
    done
    echo "no VNC client found; connect manually to 127.0.0.1:$port";;
  status)
    ensure_ctr; dexec "VMS_HOME=/vms bash /tools/vmlife.sh status";;
  all)
    shift
    for v in "${VMS[@]}"; do echo "== $v"; $0 "$@" "$v" 2>&1 | sed 's/^/   /' || true; done;;
  *) echo "commands: up mkarchbase mkvm start stop logs shot key keyseq ssh vnc status all" >&2; exit 2;;
esac
