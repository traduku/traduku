#!/usr/bin/env bash
# VM grid controller (runs on the HOST; talks to the traduku-vmm container).
# Grid: debian-kde debian-gnome ubuntu-kde ubuntu-gnome
# Each VM: qcow2 backing the distro cloud image + NoCloud seed ISO.
# Access: ssh 127.0.0.1:2201..2204 (vm/vm + ~/vms/id_ed25519), VNC :5901..5904,
# QMP sockets at ~/vms/run/<vm>/qmp.sock for screenshots + key injection.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
VMS_HOME="${VMS_HOME:-$HOME/vms}"
CTR=traduku-vmm
VMS=(debian-kde debian-gnome ubuntu-kde ubuntu-gnome)

dexec() { docker exec "$CTR" bash -c "$*"; }

ensure_ctr() {
  docker ps --format '{{.Names}}' | grep -qx "$CTR" && return 0
  docker rm -f "$CTR" 2>/dev/null || true
  [ -f "$VMS_HOME/id_ed25519" ] || ssh-keygen -f "$VMS_HOME/id_ed25519" -N '' -q
  docker run -d --name "$CTR" --device /dev/kvm --network host \
    -v "$VMS_HOME:/vms" -v "$HERE:/tools:ro" traduku-vmm sleep infinity >/dev/null
  echo "container $CTR started"
}

idx_of() { local i=1; for v in "${VMS[@]}"; do [ "$v" = "$1" ] && { echo $i; return; }; i=$((i+1)); done; echo "unknown vm $1" >&2; return 1; }

cmd=${1:-status}; shift || true
case "$cmd" in
  up) ensure_ctr;;
  mkvm)
    [ $# -eq 3 ] || { echo "usage: vmctl.sh mkvm <vm> <distro> <de:kde|gnome>" >&2; exit 2; }
    ensure_ctr; dexec "VMS_HOME=/vms bash /tools/mkvm.sh $*";;
  start|stop|logs|shot|key|keyseq)
    [ $# -ge 1 ] || { echo "usage: vmctl.sh $cmd <vm> [args]" >&2; exit 2; }
    ensure_ctr; dexec "VMS_HOME=/vms bash /tools/vmlife.sh $cmd $*";;
  ssh)
    vm=$1; shift
    ssh -i "$VMS_HOME/id_ed25519" -p $((2200+$(idx_of "$vm"))) \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o ConnectTimeout=10 "vm@127.0.0.1" "$@";;
  status)
    ensure_ctr; dexec "VMS_HOME=/vms bash /tools/vmlife.sh status";;
  all)
    shift
    for v in "${VMS[@]}"; do echo "== $v"; $0 "$@" "$v" 2>&1 | sed 's/^/   /' || true; done;;
  *) echo "commands: up mkvm start stop logs shot key keyseq ssh status" >&2; exit 2;;
esac
