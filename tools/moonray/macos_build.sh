#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Task 0111 (ADR 0041): MoonRay on macOS (Apple silicon), in the layout its macOS build instructions use:
#   /Applications/MoonRay/{source/openmoonray, openmoonray -> source/openmoonray, building, installs, build, build-deps}
#   macos_build.sh deps        the superbuild's dependencies into installs/ (cached by the workflow)
#   macos_build.sh moonray     MoonRay itself into installs/openmoonray
# The openmoonray checkout must already be at /Applications/MoonRay/source/openmoonray.
set -euo pipefail
M=/Applications/MoonRay
cd "$M"
mkdir -p installs/{bin,lib,include} build build-deps
ln -sfn source/openmoonray/building building
ln -sfn source/openmoonray openmoonray
case "$1" in
  deps)
    cd build-deps
    cmake ../building/macOS
    cmake --build . -- -j"$(sysctl -n hw.ncpu)"
    mkdir -p "$M/installs/licenses"
    for d in "$M"/build-deps/*-prefix/src/*; do
      name=$(basename "$d")
      case "$name" in *-build|*-stamp|*-tmp|*.tar.gz|*.zip) continue ;; esac
      for f in "$d"/LICENSE* "$d"/LICENCE* "$d"/COPYING* "$d"/license* "$d"/NOTICE*; do
        [ -f "$f" ] || continue
        mkdir -p "$M/installs/licenses/$name"
        cp "$f" "$M/installs/licenses/$name/"
      done
    done
    rm -rf "$M/build-deps"
    du -sh "$M/installs"
    ;;
  moonray)
    cd openmoonray
    cmake --preset macos-release -DMOONRAY_USE_OPTIX=NO -DBUILD_QT_APPS=NO
    cmake --build --preset macos-release -- -jobs "$(sysctl -n hw.ncpu)"
    ;;
  *)
    echo "usage: macos_build.sh deps|moonray" >&2
    exit 2
    ;;
esac
