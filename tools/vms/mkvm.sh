#!/usr/bin/env bash
# Create a VM: qcow2 backing disk (+ NoCloud seed ISO for cloud-image distros).
# Runs in the container.
# usage: mkvm.sh <vm> <distro:arch|debian|ubuntu> <de:kde|gnome>
set -euo pipefail
VMS_HOME=${VMS_HOME:-/vms}
VM=$1; DISTRO=$2; DE=$3
case "$DISTRO" in
  debian) BASE=$VMS_HOME/images/debian-13.qcow2;;
  ubuntu) BASE=$VMS_HOME/images/ubuntu-2510.qcow2;;
  arch)   BASE=$VMS_HOME/images/arch-base.qcow2;;
  *) echo "distro?" >&2; exit 2;;
esac
case "$DE" in kde|gnome) ;; *) echo "de?" >&2; exit 2;;
esac
[ -f "$BASE" ] || { echo "missing base image $BASE" >&2; exit 1; }
DIR=$VMS_HOME/run/$VM
rm -rf "$DIR"; mkdir -p "$DIR"; chmod a+rwX "$DIR"
SSHKEY=$(cat "$VMS_HOME/id_ed25519.pub")

# The Arch base image (mkarchbase.sh) ships the vm user, sddm autologin and
# the DE already: it needs no cloud seed at all.
if [ "$DISTRO" = arch ]; then
  [ "$DE" = kde ] || { echo "arch base is kde-only (extend mkarchbase for gnome)" >&2; exit 2; }
  qemu-img create -f qcow2 -F qcow2 -b "$BASE" "$DIR/disk.qcow2" >/dev/null
  qemu-img resize "$DIR/disk.qcow2" 30G >/dev/null
  echo arch > "$DIR/distro"
  chmod a+r "$VMS_HOME/images/arch-boot/"* 2>/dev/null || true
  echo "vm $VM created (base=$BASE de=$DE seed=none)"
  exit 0
fi

if [ "$DISTRO" = debian ]; then C5=libqt6core5compat6-dev; else C5=qt6-5compat-dev; fi
if [ "$DE" = kde ]; then
  DE_PKGS="sddm plasma-desktop plasma-workspace plasma-pa konsole"
  DE_SETUP=kde_setup
  [ "$DISTRO" = debian ] && DE_PKGS="kde-plasma-desktop konsole"
else
  DE_PKGS="gdm3 gnome-shell gnome-session gnome-control-center gnome-terminal nautilus ubuntu-session gnome-settings-daemon gnome-shell-extension-appindicator"
  [ "$DISTRO" = debian ] && DE_PKGS="gdm3 gnome-shell gnome-session gnome-control-center gnome-terminal nautilus gnome-settings-daemon gnome-shell-extension-appindicator"
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
  - wl-clipboard
  - python3
  - python3-pil
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
      echo 'xset s off 2>/dev/null; xset -dpms 2>/dev/null' >> /home/vm/.profile
      chown vm:vm /home/vm/.profile
      dconf update
      systemctl set-default graphical.target
      if [ "$DE_SETUP" = kde_setup ]; then
        mkdir -p /etc/sddm.conf.d
        cat > /etc/sddm.conf.d/10-autologin.conf <<'SDDM'
      [General]
      DisplayServer=wayland
      [Autologin]
      User=vm
      Session=plasma
      SDDM
        # Session-type corners found on the real grid: Debian 13 sddm cannot
        # autologin a Wayland session (-> X11), Ubuntu 25.10 Plasma 6 dropped
        # the X11 session entirely (-> Wayland). Pick whichever exists.
        if ! ls /usr/share/wayland-sessions/ 2>/dev/null | grep -qi plasma; then
          sed -i '/^DisplayServer=/d;/^Session=/d' /etc/sddm.conf.d/10-autologin.conf
          SES=\$(ls /usr/share/xsessions/ 2>/dev/null | grep -i plasma | grep -vi wayland | head -1)
          if [ -n "\$SES" ]; then
            echo "Session=\${SES%.desktop}" >> /etc/sddm.conf.d/10-autologin.conf
            echo "DisplayServer=x11" >> /etc/sddm.conf.d/10-autologin.conf
          else
            sed -i '/^DisplayServer=/d' /etc/sddm.conf.d/10-autologin.conf
            WSES=\$(ls /usr/share/wayland-sessions/ 2>/dev/null | grep -i plasma | head -1)
            echo "Session=\${WSES%.desktop}" >> /etc/sddm.conf.d/10-autologin.conf
          fi
        fi
        systemctl enable sddm
      else
        # GNOME boots on Wayland, which is the whole reason this shell exists:
        # the /dev/input gesture and wl-clipboard are the Wayland answers. On an
        # Xorg GNOME guest the app falls back to the X clipboard and the X
        # listener and the suite goes green without having tested the code it
        # was written for - so Wayland is the default and Xorg is the opt-in.
        # GRID_GNOME_X11=1 ./tools/vms/mkvm.sh ubuntu-gnome ubuntu gnome
        # buys the old Xorg session back (mutter with no GL in a VM is slow).
        cat >> /etc/gdm3/custom.conf <<GDM
      [daemon]
      AutomaticLoginEnable=True
      AutomaticLogin=vm
      GDM
        [ "${GRID_GNOME_X11:-0}" = 1 ] && sed -i '/^\[daemon\]/a WaylandEnable=False' /etc/gdm3/custom.conf
        # gdm has to be *wanted* by graphical.target, not merely installed.
        # Ubuntu 25.10 ships gdm3.service with no [Install] section (the distro
        # starts it through the display-manager.service alias, which a cloud
        # image does not pull in), so `systemctl enable gdm3` prints a lecture
        # and enables nothing: the box boots to a dead tty, loginctl shows no
        # seat0 session, and the grid reports "DE shell not running" for a
        # package set that is otherwise complete. The wants symlink is what the
        # target actually reads, independent of unit metadata.
        systemctl enable gdm3 2>/dev/null || true
        DMUNIT=$(systemctl show -p FragmentPath gdm3.service 2>/dev/null | cut -d= -f2)
        [ -n "\$DMUNIT" ] && [ -f "\$DMUNIT" ] || DMUNIT=$(systemctl show -p FragmentPath gdm.service 2>/dev/null | cut -d= -f2)
        if [ -n "\$DMUNIT" ] && [ -f "\$DMUNIT" ]; then
          mkdir -p /etc/systemd/system/graphical.target.wants
          ln -sf "\$DMUNIT" "/etc/systemd/system/graphical.target.wants/$(basename "\$DMUNIT")"
          echo "grid-setup: enabled display manager \$DMUNIT"
        else
          echo "grid-setup: WARNING no gdm unit found; graphical.target will have nothing to start"
        fi
        systemctl daemon-reload
        X11SESS=gnome
        [ "$DISTRO" = ubuntu ] && X11SESS=ubuntu
        mkdir -p /var/lib/AccountsService/users
        printf '[User]\nLanguage=en_US.UTF-8\nXSession=%s\nSystemAccount=false\n' "\$X11SESS" > /var/lib/AccountsService/users/vm
        systemctl enable gdm3
      fi
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
      # First attempt adds the GTK/appindicator introspection packages the
      # GNOME layer needs (SNI tray, libnotify bridge); distros without them
      # fall back to the core set. Both branches' grids ran one of the two.
      apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config debhelper extra-cmake-modules \
        qt6-base-dev qt6-multimedia-dev $C5 \
        libkf6coreaddons-dev libkf6i18n-dev \
        libkf6statusnotifieritem-dev libkf6notifications-dev \
        libkf6globalaccel-dev libkf6dbusaddons-dev \
        libx11-dev libxi-dev libxtst-dev \
        libgtk-3-dev gir1.2-notify-3.0 gir1.2-appindicator3-0.1 2>/dev/null || \
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
