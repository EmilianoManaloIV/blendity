#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Task 0111 (ADR 0041): MoonRay's third-party dependencies, built inside a rockylinux:9 container by MoonRay's own
# superbuild (building/Rocky9). Mounts: /src the openmoonray checkout, /opt/MoonRay/installs the cached install root.
# Each dependency's licence is kept in /opt/MoonRay/installs/licenses/<name> for the archive.
set -euo pipefail
source /src/building/Rocky9/install_packages.sh --nocuda --noqt
export PATH=/installs/cmake-3.23.1-linux-x86_64/bin:$PATH
mkdir -p /work/build-deps
cd /work/build-deps
cmake /src/building/Rocky9 -DInstallRoot=/opt/MoonRay/installs
cmake --build . -- -j"$(nproc)"
mkdir -p /opt/MoonRay/installs/licenses
for d in /work/build-deps/*-prefix/src/*; do
  name=$(basename "$d")
  case "$name" in *-build|*-stamp|*-tmp|*.tar.gz|*.zip) continue ;; esac
  for f in "$d"/LICENSE* "$d"/LICENCE* "$d"/COPYING* "$d"/license* "$d"/NOTICE*; do
    [ -f "$f" ] || continue
    mkdir -p "/opt/MoonRay/installs/licenses/$name"
    cp "$f" "/opt/MoonRay/installs/licenses/$name/"
  done
done
ls /opt/MoonRay/installs/licenses
du -sh /opt/MoonRay/installs
