#!/usr/bin/env bash
# Build and install traduku into $HOME/.local (no root needed),
# so the desktop entry is registered for the Wayland/KService stack.
set -euo pipefail
cd "$(dirname "$0")"

PREFIX="${PREFIX:-$HOME/.local}"

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PREFIX"

# Make KService/knotifications find the app data in the user scope even
# when the prefix is not in system paths.
mkdir -p "$HOME/.local/share/knotifications6"
cp -f traduku.notifyrc "$HOME/.local/share/knotifications6/"
mkdir -p "$HOME/.local/share/applications"
cp -f traduku.desktop "$HOME/.local/share/applications/"
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true

# Workaround for a kwin packaging gap on some distros (e.g. CachyOS):
# kwin_eis_prompter ships WITHOUT its desktop entry, so the keyboard-access
# prompt dies with "App info not found for 'org.kde.kwin.eisprompter'".
if [ ! -f /usr/share/applications/org.kde.kwin.eisprompter.desktop ] \
   && [ ! -f /usr/share/kwin_eis_prompter.desktop ]; then
  cat > "$HOME/.local/share/applications/org.kde.kwin.eisprompter.desktop" <<'EIS'
[Desktop Entry]
Type=Application
Name=KWin Input Devices Prompt
GenericName=Permission prompt for input device control
Exec=/usr/lib/kwin_eis_prompter
Icon=input-keyboard
NoDisplay=true
Terminal=false
X-DBUS-StartupType=Unique
X-DBUS-ServiceName=org.kde.kwin.eisprompter
X-KDE-StartupNotify=false
Categories=Qt;KDE;
EIS
  kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
  echo "Installed missing org.kde.kwin.eisprompter.desktop (kwin packaging workaround)."
fi

pkill -x traduku 2>/dev/null || true

echo
echo "Installed: $PREFIX/bin/traduku"
echo "Smoke test:  QT_QPA_PLATFORM=offscreen $PREFIX/bin/traduku --selftest --live"
echo "Start:       $PREFIX/bin/traduku &"
