#!/usr/bin/env bash
# Build an Arch base qcow2 from the OFFICIAL bootstrap tarball (Arch ships no
# cloud image; this keeps the base 100% distro-official and reproducible).
# Runs in the privileged traduku-pipeline-vmm container (loop + chroot).
# Result: $VMS_HOME/images/arch-base.qcow2 — user vm/vm (wheel, NOPASSWD),
# sddm autologin Plasma (Wayland-first), sshd, systemd-networkd DHCP.
set -euo pipefail
VMS_HOME=${VMS_HOME:-/vms}
MIRROR=${ARCH_MIRROR:-https://geo.mirror.pkgbuild.com}
WORK=$VMS_HOME/build/archbase
BOOT=$WORK/ArchLinuxAI
TARGET=$WORK/root
IMG=$VMS_HOME/images/arch-base.qcow2
DISK=$WORK/disk.raw

PKGS_BASE="base linux linux-firmware grub e2fsprogs sudo openssh git curl wget
xterm x11-utils xdotool xclip wl-clipboard ttf-dejavu inetutils"
PKGS_DE="plasma-desktop plasma-workspace sddm konsole"
PKGS_BUILD="base-devel cmake ninja qt6-base qt6-multimedia qt6-5compat
qt6-tools extra-cmake-modules kstatusnotifieritem knotifications kglobalaccel
kdbusaddons libx11 libxi libxtst"

log() { echo "[mkarchbase] $*"; }
if [ -f "$IMG" ]; then log "image exists: $IMG (rm to rebuild)"; exit 0; fi
mkdir -p "$WORK" "$VMS_HOME/images"

# ---- 1. bootstrap ----------------------------------------------------------
if [ ! -d "$BOOT" ]; then
  log "downloading official bootstrap tarball"
  wget -q -O "$WORK/bootstrap.tar.zst" "$MIRROR/iso/latest/archlinux-bootstrap-x86_64.tar.zst"
  tar -I zstd -xf "$WORK/bootstrap.tar.zst" -C "$WORK"
fi
# Bootstrap ships pacman.conf with commented Server lines -> point at one mirror.
sed -i "s|^#Server = https://geo.mirror.pkgbuild.com.*\$|Server = $MIRROR/\$repo/os/\$arch|" "$BOOT/etc/pacman.conf"
grep -q '^Server' "$BOOT/etc/pacman.conf" || echo "Server = $MIRROR/\$repo/os/\$arch" >> "$BOOT/etc/pacman.conf"
log "pacman servers: $(grep -c '^Server' "$BOOT/etc/pacman.conf")"

# ---- 2. disk ---------------------------------------------------------------
log "creating raw disk + MBR partition"
rm -f "$DISK"
qemu-img create -f raw "$DISK" 26G >/dev/null
printf 'label: dos\nstart=2048, type=83, bootable\n' | sfdisk -q "$DISK"
modprobe loop 2>/dev/null || true
LOOP=$(losetup -f --show -P "$DISK")
umount -R "$TARGET" 2>/dev/null || true
cleanup() {
  for d in dev/pts dev proc sys; do umount -R "$BOOT/$d" 2>/dev/null || true; done
  umount -R "$TARGET" 2>/dev/null || true
  losetup -d "$LOOP" 2>/dev/null || true
}
trap cleanup EXIT
mkfs.ext4 -q -F "${LOOP}p1"
mkdir -p "$TARGET"
mount "${LOOP}p1" "$TARGET"

# ---- 3. bootstrap chroot mounts + keyring ---------------------------------
for d in dev proc sys; do mount --bind "/$d" "$BOOT/$d" 2>/dev/null || true; done
mkdir -p "$BOOT/mnt"
mount --bind "$TARGET" "$BOOT/mnt"
log "initialising pacman keyring"
chroot "$BOOT" /bin/sh -c '
  set -e
  rm -f /etc/pacman.d/gnupg/*.lock 2>/dev/null || true
  pacman-key --init >/dev/null 2>&1
  pacman-key --populate archlinux >/dev/null 2>&1
'

# ---- 4. install everything -------------------------------------------------
log "installing base+kde+build (downloads ~2G, takes a while)"
chroot "$BOOT" /bin/sh -c "
  set -e
  rm -f /var/lib/pacman/db.lck
  pacman -Sy --noconfirm --sysroot=/mnt $PKGS_BASE $PKGS_DE $PKGS_BUILD
"

# ---- 5. target configuration ----------------------------------------------
log "configuring target"
ROOT_UUID=$(blkid -s UUID -o value "${LOOP}p1")
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
NET

mkdir -p "$TARGET/etc/sudoers.d"
echo '%wheel ALL=(ALL) NOPASSWD: ALL' > "$TARGET/etc/sudoers.d/wheel"
chmod 440 "$TARGET/etc/sudoers.d/wheel"

chroot "$TARGET" /bin/sh -c '
  set -e
  useradd -m -G wheel -s /bin/bash vm
  echo "vm:vm" | chpasswd
  echo "root:root" | chpasswd
  mkdir -p /home/vm/.config
  printf "[Daemon]\nAutostart=false\n" > /home/vm/.config/kscreenlockerrc
  printf "export LANG=C.UTF-8\n" > /home/vm/.profile
  chown -R vm:vm /home/vm
  systemctl enable sddm sshd systemd-networkd
  true
'

# ---- 6. grub (MBR) ---------------------------------------------------------
log "grub-install -> $LOOP"
mkdir -p "$TARGET/dev"
ln -sf "$LOOP" "$TARGET/dev/$(basename "$LOOP")"
chroot "$TARGET" grub-install --force --target=i386-pc --boot-directory=/boot "$LOOP"
chroot "$TARGET" /bin/sh -c 'grub-mkconfig -o /boot/grub/grub.cfg'

# ---- 7. qcow2 --------------------------------------------------------------
log "converting to qcow2"
umount "$TARGET"
losetup -d "$LOOP"
qemu-img convert -f raw -O qcow2 "$DISK" "$IMG"
log "done: $IMG"
