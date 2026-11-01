FROM debian:13
RUN apt-get update && apt-get install -y --no-install-recommends \
    qemu-system-x86 qemu-utils cloud-image-utils genisoimage \
    openssh-client wget ca-certificates python3 python3-pil iproute2 \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /vms
