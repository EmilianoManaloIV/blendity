#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

if [[ $# != 1 || ${1:0:1} != / ]]; then
    echo 'usage: bash tools/dev/ply_import_probe.sh ABSOLUTE_NEW_EXECUTABLE' >&2
    exit 2
fi
output=$1
if [[ -e "$output" || -L "$output" || ! -d "$(dirname -- "$output")" ]]; then
    echo 'probe executable must be a new file in an existing scratch directory' >&2
    exit 2
fi
cd -- "$(dirname -- "$0")/../.."
read -r -a compiler <<< "${CXX:-c++}"

# Keep the release optimization level and production importer sources. The linker drops the unused SPZ reader,
# which otherwise needs the image and package importers, and unrelated code in these translation units.
"${compiler[@]}" -std=c++17 -O2 -DNDEBUG -Wall -Wextra -Wno-unused-parameter \
    -pthread -ffunction-sections -fdata-sections -Wl,--gc-sections \
    tools/dev/ply_import_probe.cpp src/scene/import_splat.cpp src/scene/pointcloud.cpp \
    src/core/core.cpp src/core/jobs.cpp -ldl -o "$output"
