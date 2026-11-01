#!/usr/bin/env bash
# Create a VM: qcow2 backing disk + NoCloud seed ISO. Runs in the container.
# usage: mkvm.sh <vm> <distro:debian|ubuntu> <de:kde|gnome>
set -euo pipefail
VMS_HOME=${VMS_HOME:-/vms}
VM=$1; DISTRO=$2; DE=$3
case "$DISTRO" in
  debian) BASE=$VMS_HOME/images/debian-13.qcow2;;
  ubuntu) BASE=$VMS_HOME/images/ubuntu-2510.qcow2;;
  *) echo "distro?" >&2; exit 2;;
esac
case "$DE" in kde|gnome) ;; *) echo "de?" >&2; exit 2;;
esac
[ -f "$BASE" ] || { echo "missing base image $BASE" >&2; exit 1; }
DIR=$VMS_HOME/run/$VM
mkdir -p "$DIR"
SSHKEY=$(cat "$VMS_HOME/id_ed25519.pub")

# Per-DE packages / setup, per-distro Qt Core5Compat dev package name.
if [ "$DISTRO" = debian ]; then C5=libqt6core5compat6-dev; else C5=qt6-5compat-dev; fi
if [ "$DE" = kde ]; then
  DE_PKGS="sddm plasma-desktop plasma-workspace plasma-pa konsole"
  DE_SETUP=kde_setup
  [ "$DISTRO" = debian ] && DE_PKGS="kde-plasma-desktop konsole"
else
  DE_PKGS="gdm3 gnome-shell gnome-session gnome-control-center gnome-terminal nautilus ubuntu-session gnome-settings-daemon"
  [ "$DISTRO" = debian ] && DE_PKGS="gdm3 gnome-shell gnome-session gnome-control-center gnome-terminal nautilus gnome-settings-daemon"
  DE_SETUP=gnome_setup
fi

cat > "$DIR/user-data" <<EOF
#cloud-config
package_update: true
users:
  - name: vm
    plain_text_passwd: vm
    groups: [sudo]
    shell: /bin/bash
    lock_passwd: false
    ssh_authorized_keys:
      - $SSHKEY
chpasswd:
  expire: false
packages:
  - openssh-server
  - x11-utils
  - xterm
  - fonts-dejavu-core
  - xdotool
  - xclip
  - python3
  - python3-pyqt5
  - onboard
$(for p in $DE_PKGS; do echo "  - $p"; done)
write_files:
  - path: /etc/dconf/db/local.d/00-grid
    permissions: '0644'
    content: |
      [org/gnome/desktop/session]
      idle-delay=uint32 0
      [org/gnome/desktop/screensaver]
      lock-enabled=false
      idle-activation-enabled=false
  - path: /etc/dconf/profile/user
    permissions: '0644'
    content: |
      user-db:user
      system-db:local
  - path: /usr/local/bin/grid-setup.sh
    permissions: '0755'
    content: |
      #!/bin/bash
      set -x
      echo "grid-setup: $DE_SETUP for $DISTRO"
      # Never blank the console (VNC/QMP screenshots rely on it).
      echo 'xset s off 2>/dev/null; xset -dpms 2>/dev/null' >> /home/vm/.profile
      chown vm:vm /home/vm/.profile
      dconf update
      systemctl set-default graphical.target
      if [ "$DE_SETUP" = kde_setup ]; then
        mkdir -p /etc/sddm.conf.d
        cat > /etc/sddm.conf.d/10-autologin.conf <<'SDDM'
      [General]
      DisplayServer=x11
      [Autologin]
      User=vm
      SDDM
        SES=\$(ls /usr/share/xsessions/ 2>/dev/null | grep -i plasma | grep -vi wayland | head -1)
        if [ -n "\$SES" ]; then
          echo "Session=\${SES%.desktop}" >> /etc/sddm.conf.d/10-autologin.conf
        else
          # Plasma 6 dropped X11 here (Ubuntu 25.10): autologin the Wayland session.
          sed -i '/^DisplayServer=/d' /etc/sddm.conf.d/10-autologin.conf
          WSES=\$(ls /usr/share/wayland-sessions/ 2>/dev/null | grep -i plasma | head -1)
          echo "Session=\${WSES%.desktop}" >> /etc/sddm.conf.d/10-autologin.conf
        fi
        systemctl enable sddm
      else
        cat >> /etc/gdm3/custom.conf <<GDM
      [daemon]
      WaylandEnable=False
      AutomaticLoginEnable=True
      AutomaticLogin=vm
      GDM
        X11SESS=gnome
        [ "$DISTRO" = ubuntu ] && X11SESS=ubuntu
        mkdir -p /var/lib/AccountsService/users
        printf '[User]\nLanguage=en_US.UTF-8\nXSession=%s\nSystemAccount=false\n' "\$X11SESS" > /var/lib/AccountsService/users/vm
        systemctl enable gdm3
      fi
      # KDE: no idle screen lock
      mkdir -p /home/vm/.config
      printf '[Daemon]\nAutostart=false\n' > /home/vm/.config/kscreenlockerrc
      chown -R vm:vm /home/vm/.config
  - path: /usr/local/bin/install-deps.sh
    permissions: '0755'
    content: |
      #!/bin/bash
      set -x
      export DEBIAN_FRONTEND=noninteractive
      apt-get update
      apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config debhelper extra-cmake-modules \
        qt6-base-dev qt6-multimedia-dev $C5 \
        libkf6coreaddons-dev libkf6i18n-dev \
        libkf6statusnotifieritem-dev libkf6notifications-dev \
        libkf6globalaccel-dev libkf6dbusaddons-dev \
        libx11-dev libxi-dev libxtst-dev \
        libgtk-3-dev gir1.2-notify-3.0 gir1.2-appindicator3-0.1 gir1.2-gnomebg-1.0 2>/dev/null || \
      apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config debhelper extra-cmake-modules \
        qt6-base-dev qt6-multimedia-dev $C5 \
        libkf6coreaddons-dev libkf6i18n-dev \
        libkf6statusnotifieritem-dev libkf6notifications-dev \
        libkf6globalaccel-dev libkf6dbusaddons-dev \
        libx11-dev libxi-dev libxtst-dev
sudo: ALL=(ALL) NOPASSWD:ALL
runcmd:
  - [bash, /usr/local/bin/grid-setup.sh]
  - [bash, /usr/local/bin/install-deps.sh]
EOF

cat > "$DIR/meta-data" <<EOF
instance-id: iid-$VM-$(date +%s)
local-hostname: $VM
EOF

genisoimage -quiet -o "$DIR/seed.iso" -V CIDATA -J -rock \
  -r -input-charset utf-8 "$DIR/user-data" "$DIR/meta-data" 2>/dev/null || \
  cloud-localds "$DIR/seed.iso" "$DIR/user-data" "$DIR/meta-data"

qemu-img create -f qcow2 -F qcow2 -b "$BASE" "$DIR/disk.qcow2" >/dev/null
qemu-img resize "$DIR/disk.qcow2" 25G >/dev/null
echo "vm $VM created (base=$BASE de=$DE)"
