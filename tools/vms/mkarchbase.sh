#!/usr/bin/env bash
# Build an Arch base qcow2 from the OFFICIAL bootstrap tarball (Arch ships no
# cloud image; this keeps the base 100% distro-official and reproducible).
# Runs in the privileged traduku-pipeline-vmm container (loop + chroot).
# Result: $VMS_HOME/images/arch-base.qcow2 — user vm/vm (wheel, NOPASSWD),
# sddm autologin Plasma (Wayland-first), sshd, systemd-networkd DHCP.
# Phases are marked ($WORK/.phase-*) so an interrupted run resumes in place.
set -euo pipefail
VMS_HOME=${VMS_HOME:-/vms}
MIRROR=${ARCH_MIRROR:-https://geo.mirror.pkgbuild.com}
WORK=$VMS_HOME/build/archbase
TARGET=$WORK/root
IMG=$VMS_HOME/images/arch-base.qcow2
DISK=$WORK/disk.raw
PART_OFF=1048576   # sector 2048

PKGS_BASE="base linux linux-firmware grub e2fsprogs sudo openssh git curl wget xterm xdotool xclip xorg-xprop xorg-xsetroot wl-clipboard ttf-dejavu inetutils qt6-wayland xorg-xwininfo xorg-xdpyinfo"
PKGS_DE="plasma-desktop plasma-workspace sddm konsole"
PKGS_BUILD="base-devel cmake ninja qt6-base qt6-multimedia qt6-5compat qt6-tools extra-cmake-modules knotifications kglobalaccel kdbusaddons libx11 libxi libxtst"

log() { echo "[mkarchbase] $*"; }
[ -f "$IMG" ] && { log "image exists: $IMG (rm to rebuild)"; exit 0; }
mkdir -p "$WORK" "$VMS_HOME/images"
done_phase() { touch "$WORK/.phase-$1"; }
has_phase() { [ -f "$WORK/.phase-$1" ]; }

# ---- 1. bootstrap ----------------------------------------------------------
if ! has_phase install; then
  BOOT=$WORK/bootstrap
  if [ ! -d "$WORK/bootstrap" ]; then
    log "downloading official bootstrap tarball"
    wget -q -O "$WORK/bootstrap.tar.zst" "$MIRROR/iso/latest/archlinux-bootstrap-x86_64.tar.zst"
    mkdir -p "$WORK/bootstrap"
    tar -I zstd -xf "$WORK/bootstrap.tar.zst" -C "$WORK/bootstrap"
  fi
  # The tarball root is root.x86_64/ (older releases: ArchLinuxAI/)
  BOOT=$(ls -d "$WORK/bootstrap"/*/ | head -1)
  BOOT=${BOOT%/}
  log "bootstrap root: $BOOT"
  sed -i "s|^#Server = https://geo.mirror.pkgbuild.com.*\$|Server = $MIRROR/\$repo/os/\$arch|" "$BOOT/etc/pacman.conf"
  grep -q '^Server' "$BOOT/etc/pacman.conf" || echo "Server = $MIRROR/\$repo/os/\$arch" >> "$BOOT/etc/pacman.conf"

  # ---- 2. disk -------------------------------------------------------------
  log "creating raw disk + MBR partition"
  rm -f "$DISK"
  qemu-img create -f raw "$DISK" 26G >/dev/null
  printf 'label: dos\nstart=2048, type=83, bootable\n' | sfdisk -q "$DISK"
fi

modprobe loop 2>/dev/null || true
# Privileged containers get a thin /dev: create loop nodes (minor = N*16).
for i in $(seq 0 15); do [ -e /dev/loop$i ] || mknod /dev/loop$i b 7 $((i*16)) 2>/dev/null || true; done
LOOP=$(losetup -f --show "$DISK")
LOOP2=$(losetup -f --show -o $PART_OFF --sizelimit $((25*1024*1024*1024)) "$DISK")
cleanup() {
  BOOT=${BOOT:-/nonexistent-bootstrap}
  for d in dev/pts dev proc sys; do umount -R "$BOOT/$d" 2>/dev/null || true; done
  umount -R "$TARGET" 2>/dev/null || true
  umount "$BOOT/mnt" 2>/dev/null || true
  losetup -d "$LOOP2" 2>/dev/null || true
  losetup -d "$LOOP" 2>/dev/null || true
}
trap cleanup EXIT

if ! has_phase install; then
  mkfs.ext4 -q -F "$LOOP2"
fi
mkdir -p "$TARGET"
mountpoint -q "$TARGET" || mount "$LOOP2" "$TARGET"

if ! has_phase install; then
  BOOT=$(ls -d "$WORK/bootstrap"/*/ | head -1); BOOT=${BOOT%/}
  for d in dev proc sys; do mount --bind "/$d" "$BOOT/$d" 2>/dev/null || true; done
  cp /etc/resolv.conf "$BOOT/etc/resolv.conf"
  mkdir -p "$BOOT/mnt"
  mount --bind "$TARGET" "$BOOT/mnt"
  log "initialising pacman keyring"
  chroot "$BOOT" /bin/sh -c '
    set -e
    rm -f /etc/pacman.d/gnupg/*.lock 2>/dev/null || true
    pacman-key --init >/dev/null 2>&1
    pacman-key --populate archlinux >/dev/null 2>&1
  '
  log "installing base+kde+build (downloads ~2G, takes a while)"
  # pacman 7 resolves --config/GPGDir relative to --sysroot: ship the target
  # its own pacman.conf. Bootstrap pass over the official HTTPS mirror with
  # SigLevel=Never (keyring bootstrapping); the real keyring is populated
  # inside the target right after.
  mkdir -p "$TARGET/etc" "$TARGET/var/lib/pacman" "$TARGET/var/cache/pacman/pkg"
  cat > "$TARGET/etc/pacman.conf" <<CFG
[options]
RootDir = /
DBPath = /var/lib/pacman/
CacheDir = /var/cache/pacman/pkg/
LogFile = /var/log/pacman.log
GPGDir = /etc/pacman.d/gnupg/
HookDir = /usr/share/libalpm/hooks/
Architecture = auto
CheckSpace
SigLevel = Never

[core]
Server = $MIRROR/\$repo/os/\$arch

[extra]
Server = $MIRROR/\$repo/os/\$arch
CFG
  chroot "$BOOT" /bin/sh -c "
    set -e
    rm -f /var/lib/pacman/db.lck /mnt/var/lib/pacman/db.lck
    pacman -Sy --noconfirm --sysroot=/mnt $PKGS_BASE $PKGS_DE $PKGS_BUILD
  "
  done_phase install
fi

# ---- 5. target configuration ----------------------------------------------
if ! has_phase config; then
  log "configuring target"
  ROOT_UUID=$(blkid -s UUID -o value "$LOOP2")
  printf 'UUID=%s / ext4 rw,relatime 0 1\n' "$ROOT_UUID" > "$TARGET/etc/fstab"

  mkdir -p "$TARGET/etc/sddm.conf.d"
  cat > "$TARGET/etc/sddm.conf.d/10-grid.conf" <<'SDDM'
[General]
DisplayServer=wayland
[Autologin]
User=vm
Session=plasma
SDDM

  cat > "$TARGET/etc/systemd/network/99-dhcp.network" <<'NET'
[Match]
Name=eno* enp* eth*
[Network]
DHCP=yes
DNS=10.0.2.3
NET

  mkdir -p "$TARGET/etc/sudoers.d"
  echo '%wheel ALL=(ALL) NOPASSWD: ALL' > "$TARGET/etc/sudoers.d/wheel"
  chmod 440 "$TARGET/etc/sudoers.d/wheel"

  chroot "$TARGET" /bin/sh -c '
    set -e
    sed -i "s/^SigLevel = Never/SigLevel = Required DatabaseOptional/" /etc/pacman.conf
    rm -rf /etc/pacman.d/gnupg
    install -d -m 700 -o root -g root /etc/pacman.d/gnupg
    export LC_ALL=C
    pacman-key --init >/dev/null
    pacman-key --populate archlinux >/dev/null
    id vm >/dev/null 2>&1 || useradd -m -G wheel -s /bin/bash vm
    echo "vm:vm" | chpasswd
    echo "root:root" | chpasswd
    mkdir -p /home/vm/.config
    printf "[Daemon]\nAutostart=false\n" > /home/vm/.config/kscreenlockerrc
    printf "export LANG=C.UTF-8\n" > /home/vm/.profile
    chown -R vm:vm /home/vm
    systemctl enable sddm sshd systemd-networkd systemd-resolved
    true
  '
  done_phase config
fi

# ---- 6. bootloader: none. The grid boots the kernel via QEMU -kernel/-append
# (mkvm copies images/arch-boot/* into the VM dir). No grub-on-loop probing.
if ! has_phase grub; then
  BOOTOUT=$VMS_HOME/images/arch-boot
  mkdir -p "$BOOTOUT"
  mount --bind /dev "$TARGET/dev"; mount --bind /proc "$TARGET/proc" 2>/dev/null || true
  chroot "$TARGET" mkinitcpio -p linux
  KVMLZ=$(ls "$TARGET"/usr/lib/modules/*/vmlinuz | head -1)
  cp "$KVMLZ" "$BOOTOUT/vmlinuz-linux"
  cp "$TARGET/boot/initramfs-linux.img" "$BOOTOUT/initramfs-linux.img"
  blkid -s UUID -o value "$LOOP2" > "$BOOTOUT/root.uuid"
  done_phase grub
fi

# ---- 7. qcow2 --------------------------------------------------------------
log "converting to qcow2"
umount "$TARGET"
losetup -d "$LOOP2"
losetup -d "$LOOP"
qemu-img convert -f raw -O qcow2 "$DISK" "$IMG"
log "done: $IMG"
