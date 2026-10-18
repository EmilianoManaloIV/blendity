#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Task 0111 (ADR 0041): MoonRay itself, built inside a rockylinux:9 container against the dependencies linux_deps.sh
# installed, then packaged for Blendity by package_linux.sh. Mounts: /src the openmoonray checkout,
# /opt/MoonRay/installs the dependencies, /tools this folder, /out where the archive goes.
set -euo pipefail
source /src/building/Rocky9/install_packages.sh --nocuda --noqt --nocgroup  # (libcgroup: for Arras only; its RPM link is gone)
export PATH=/installs/cmake-3.23.1-linux-x86_64/bin:$PATH
cd /src
# (The preset builds in /build and installs into /installs/openmoonray; no GPU, no Qt apps.)
cmake --preset rocky9-release -DMOONRAY_USE_OPTIX=NO -DBUILD_QT_APPS=NO
cmake --build --preset rocky9-release -- -j"$(nproc)"
rm -rf /build
bash /tools/package_linux.sh /installs/openmoonray /out
