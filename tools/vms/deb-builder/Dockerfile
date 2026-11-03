# traduku-deb-builder - trixie toolchain + Qt6/KF6 dev set for the .deb
# gate in tools/vms/vm-install.sh. Baked once; rebuilds then never touch
# the network (apt stalls used to hang dpkg-buildpackage runs).
#   docker build -t traduku-deb-builder:trixie tools/vms/deb-builder
FROM debian:13
# Mirror pin: deb.debian.org routed us to a ~5 KB/s node while ftp.debian.org
# gave ~1.4 MB/s (measured; both degrade under CDN health wobbles). Retries/
# Timeout: plain apt-get hung on the stall; fail fast instead. The cache
# mount keeps partially downloaded .debs ACROSS retries - with a flaky
# uplink, attempts accumulate instead of restarting from zero.
RUN sed -i 's|^URIs: http://deb.debian.org/debian$|URIs: http://ftp.debian.org/debian|' /etc/apt/sources.list.d/debian.sources \
    && grep -a "^URIs" /etc/apt/sources.list.d/debian.sources
RUN --mount=type=cache,target=/var/cache/apt \
    apt-get update -qq \
      -o Acquire::Retries=5 -o Acquire::http::Timeout=15 \
    && apt-get install -y -qq --no-install-recommends \
      -o Acquire::Retries=5 -o Acquire::http::Timeout=15 \
      build-essential cmake ninja-build pkg-config debhelper dpkg-dev \
      extra-cmake-modules \
      qt6-base-dev qt6-multimedia-dev libqt6core5compat6-dev \
      libkf6coreaddons-dev libkf6i18n-dev libkf6statusnotifieritem-dev \
      libkf6notifications-dev libkf6globalaccel-dev libkf6dbusaddons-dev \
      libx11-dev libxi-dev libxtst-dev \
    && rm -rf /var/lib/apt/lists/*

