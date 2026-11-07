# traduku-deb-builder - trixie toolchain + Qt6/KF6 dev set for the .deb
# gate in tools/vms/vm-install.sh. Baked once; rebuilds then never touch
# the network (apt stalls used to hang dpkg-buildpackage runs).
#   docker build -t traduku-deb-builder:trixie tools/vms/deb-builder
FROM debian:13
# Mirror pin: deb.debian.org routed us to a ~5 KB/s node while ftp.debian.org
# gave ~1.4 MB/s (measured; both degrade under CDN health wobbles). Retries/
# Timeout: plain apt-get hung on the stall; fail fast instead, and let the
# caller retry (vm-install builds on demand; docker layer cache skips the
# expensive layers on every later run). NOTE: no BuildKit here - the host
# docker is legacy (buildx missing), so --mount=type=cache is unavailable.
RUN sed -i 's|^URIs: http://deb.debian.org/debian$|URIs: http://ftp.debian.org/debian|' /etc/apt/sources.list.d/debian.sources \
    && grep -a "^URIs" /etc/apt/sources.list.d/debian.sources
RUN apt-get update -qq \
      -o Acquire::Retries=5 -o Acquire::http::Timeout=15 \
    && apt-get install -y -qq --no-install-recommends \
      -o Acquire::Retries=5 -o Acquire::http::Timeout=15 \
      build-essential cmake ninja-build pkg-config debhelper dpkg-dev \
      extra-cmake-modules \
      qt6-base-dev qt6-multimedia-dev libqt6core5compat6-dev \
      libkf6coreaddons-dev libkf6i18n-dev libkf6statusnotifieritem-dev \
      libkf6notifications-dev libkf6globalaccel-dev libkf6dbusaddons-dev \
      libx11-dev libxi-dev libxtst-dev \
      # An X server for the clipboard integration tests. They are not
      # optional there: tests/integration/tst_x11clipboard skips when no
      # server is reachable, and a gate that skips everything passes for the
      # wrong reason - so the gate provides a server and sets
      # TRADUKU_REQUIRE_X11=1 to make "no server" a failure. See
      # tools/vms/x11-ctest.sh.
      xvfb x11-utils \
    && rm -rf /var/lib/apt/lists/*

