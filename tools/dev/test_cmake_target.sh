#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Generate the real Linux targets with simulated toolchain metadata; compile nothing.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$repo/build"
work="$(mktemp -d "$repo/build/cmake-target-XXXXXX")"
trap 'rm -rf "$work"' EXIT
project="$work/project"
x64="$work/blender/lib/linux_x64"
arm="$work/arm"
mkdir -p "$project" "$work/modules" "$x64/tbb/include" "$arm/tbb/include"
cp "$repo"/{CMakeLists.txt,VERSION} "$project"
for dir in src extern stress tests; do ln -s "$repo/$dir" "$project/$dir"; done

# No system SDK is required. Threads and X11 are unrelated to bundle discovery.
echo 'add_library(Threads::Threads INTERFACE IMPORTED)' > "$work/modules/FindThreads.cmake"
touch "$work/modules/FindX11.cmake"
cat > "$work/toolchain.cmake" <<'CMAKE'
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR "$ENV{TEST_PROCESSOR}")
set(CMAKE_SIZEOF_VOID_P 8)
foreach(lang C CXX)
  set(CMAKE_${lang}_COMPILER /bin/true)
  set(CMAKE_${lang}_COMPILER_ID_RUN TRUE)
  set(CMAKE_${lang}_COMPILER_ID GNU)
  set(CMAKE_${lang}_COMPILER_FORCED TRUE)
endforeach()
CMAKE

check_config() {
  local name="$1" processor="$2" bundle="$3" sse="$4"
  shift 4
  local out="$work/$name" expected actual flag
  TEST_PROCESSOR="$processor" cmake -S "$project" -B "$out" -G 'Unix Makefiles' \
    -DCMAKE_TOOLCHAIN_FILE="$work/toolchain.cmake" -DCMAKE_MODULE_PATH="$work/modules" "$@" > "$work/config.log" ||
    { cat "$work/config.log" >&2; return 1; }
  local flags="$out/CMakeFiles/blendity_engine.dir/flags.make"
  for flag in -DBL_WITH_TBB -msse4.2 -mpopcnt -DJPH_USE_SSE4_1 -DJPH_USE_SSE4_2; do
    expected="$sse"
    if [[ "$flag" == -DBL_WITH_TBB ]]; then
      expected=absent
      if [[ -n "$bundle" ]]; then expected=present; fi
    fi
    actual=absent
    if grep -Fwq -- "$flag" "$flags"; then actual=present; fi
    if [[ "$actual" != "$expected" ]]; then echo "$name: $flag $actual, expected $expected" >&2; return 1; fi
  done
  local link="$out/CMakeFiles/Blendity.dir/link.txt"
  if [[ -n "$bundle" ]]; then
    grep -Fq "/${bundle##*/}/tbb/include" "$flags" || { echo "$name: incorrect TBB include path" >&2; return 1; }
    grep -Fq "/${bundle##*/}/tbb/lib" "$link" || { echo "$name: incorrect TBB link path" >&2; return 1; }
  elif grep -Fq 'tbb/lib' "$link"; then
    echo "$name: unexpected TBB link path" >&2; return 1
  fi
}
check_config native_x64 x86_64 "$x64" present
check_config native_arm aarch64 '' absent
check_config explicit_arm aarch64 "$arm" absent -DBLENDITY_LIBDIR="$arm"
check_config stale_arm aarch64 '' absent -DBLENDITY_LIBDIR:PATH="$project/../blender/lib/linux_x64"
check_config no_libs x86_64 '' absent -DBLENDITY_NO_LIBS=ON
echo 'Linux CMake target selection passed'
