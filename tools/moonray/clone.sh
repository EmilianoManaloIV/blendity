#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Task 0111: MoonRay's source at $MOONRAY_TAG with its submodules, into $1. materialx_shaders is left out: its
# repository isn't public, and MoonRay builds it only with BUILD_MATERIALX_SHADERS (off by default).
set -euo pipefail
git clone --depth 1 --branch "$MOONRAY_TAG" https://github.com/dreamworksanimation/openmoonray.git "$1"
cd "$1"
git -c submodule.moonray/materialx_shaders.update=none submodule update --init --recursive --jobs 8
git submodule status --recursive | head -40
