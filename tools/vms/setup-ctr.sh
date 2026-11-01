#!/usr/bin/env bash
# One-time: build the vmm image and start the privileged grid container.
# Privileged: the Arch base builder needs loop devices + chroot/pivot_root.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
VMS_HOME="${VMS_HOME:-$HOME/vms}"
CTR=traduku-pipeline-vmm
docker build -t traduku-pipeline-vmm:latest "$HERE"
[ -f "$VMS_HOME/id_ed25519" ] || ssh-keygen -f "$VMS_HOME/id_ed25519" -N '' -q
docker rm -f "$CTR" 2>/dev/null || true
docker run -d --name "$CTR" --privileged --device /dev/kvm --network host \
  -v "$VMS_HOME:/vms" -v "$HERE:/tools:ro" traduku-pipeline-vmm:latest sleep infinity
echo "container $CTR ready"
