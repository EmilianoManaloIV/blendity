#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Blender's prebuilt libraries for one platform (task 0078): the folders Blendity links, at the commits pinned in
# packaging/blender-libs.txt, as sparse shallow clones (only those folders, only that commit), into Blender's layout
# next to this project: ../blender/lib/<platform> (reusing a clone already there) and, when missing,
# ../blender/release/datafiles/colormanagement. The build scripts then use them by themselves. Needs git with git-lfs
# (the libraries are LFS files). About 600 MB a platform.
#   packaging/fetch_blender_libs.sh windows_x64|linux_x64|macos_arm64 [blender folder]
set -euo pipefail
cd "$(dirname "$0")/.."
PLAT="${1:?usage: fetch_blender_libs.sh windows_x64|linux_x64|macos_arm64 [blender folder]}"
BL="${2:-../blender}"
pin() { awk -v n="$1" '$1 == n { print $2 }' packaging/blender-libs.txt; }
[ -n "$(pin "$PLAT")" ] || { echo "no pinned commit for $PLAT in packaging/blender-libs.txt" >&2; exit 1; }

# What build.bat / build.sh link (and their runtime-only dependencies).
FOLDERS="tbb embree openimagedenoise eigen opensubdiv openexr imath openjph opencolorio manifold meshoptimizer openpgl"
FOLDERS="$FOLDERS jolt jpeg png zlib zstd shaderc vulkan ffmpeg"
case "$PLAT" in
  windows_x64) FOLDERS="$FOLDERS aom" ;;       # (FFmpeg's AV1 DLL)
  linux_x64) FOLDERS="$FOLDERS dpcpp openjpeg" ;;  # (Embree's SYCL runtime; inside FFmpeg's static build)
  macos_arm64) FOLDERS="$FOLDERS openjpeg" ;;
esac

# A sparse, shallow, blob-less clone of `url` at `commit` holding only the given folders, in `dest`.
sparse_fetch() {
  local url="$1" dest="$2" commit="$3"
  shift 3
  if [ ! -d "$dest/.git" ]; then
    mkdir -p "$dest"
    git -C "$dest" init -q
    git -C "$dest" remote add origin "$url"
  fi
  git -C "$dest" lfs install --local >/dev/null
  git -C "$dest" sparse-checkout set --cone "$@"
  git -C "$dest" fetch -q --depth 1 --filter=blob:none origin "$commit"
  git -C "$dest" checkout -q -f FETCH_HEAD
}

echo "[fetch] lib-$PLAT at $(pin "$PLAT"): $FOLDERS"
# shellcheck disable=SC2086
sparse_fetch "https://projects.blender.org/blender/lib-$PLAT.git" "$BL/lib/$PLAT" "$(pin "$PLAT")" $FOLDERS
# The colour config comes from Blender's own repository, fetched aside (never into a Blender source folder that may
# be there already) and copied in when it's missing.
CM="$BL/release/datafiles/colormanagement"
if [ ! -f "$CM/config.ocio" ]; then
  echo "[fetch] Blender's colour management config at $(pin blender)"
  sparse_fetch "https://projects.blender.org/blender/blender.git" build/blender-datafiles "$(pin blender)" release/datafiles/colormanagement
  mkdir -p "$BL/release/datafiles"
  cp -R build/blender-datafiles/release/datafiles/colormanagement "$BL/release/datafiles/"
fi
echo "[fetch] done: $(du -sh "$BL/lib/$PLAT" | cut -f1) in $BL/lib/$PLAT"
