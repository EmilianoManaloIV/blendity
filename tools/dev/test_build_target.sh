#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Exercise build.sh's real discovery and link arguments without requiring a cross SDK.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/blender/lib/linux_x64/tbb/include" "$work/arm/tbb/include" "$work/project"
cd "$work/project"
cp "$repo"/{build.sh,VERSION} .
mkdir -p licenses tests src/{core,image,render,scene,texture,editor,research,deps}
touch tests/fixture.cpp src/{core,image,render,scene,texture,editor,research,deps}/fixture.cpp
cat > "$work/compiler" <<'SH'
#!/bin/sh
case " $* " in
  *' -dumpmachine '*) echo "$TEST_TARGET" ;;
  *' -o dist/linux/Blendity '*) printf '%s\n' "$@" > link-args ;;
esac
SH
chmod +x "$work/compiler"
export CC="$work/compiler" CXX="$work/compiler" BLENDITY_NO_WARM=1 BLENDITY_NO_OPTIX=1
unset EXTRA_FLAGS BLENDITY_LIBDIR BLENDITY_NO_LIBS BLENDITY_APP BLENDITY_TRACY BLENDITY_ALLOCATOR
check_link() {
  rm -f link-args
  bash build.sh release app > build.log
  test -s link-args
  for flag in -DBL_WITH_TBB -msse4.2 -mpopcnt -DJPH_USE_SSE4_1 -DJPH_USE_SSE4_2; do
    expected="$1"
    if [ "$flag" != -DBL_WITH_TBB ]; then expected="$2"; fi
    actual=absent
    if grep -Fxq -- "$flag" link-args; then actual=present; fi
    if [ "$actual" != "$expected" ]; then echo "$TEST_TARGET: $flag $actual, expected $expected" >&2; exit 1; fi
  done
}
export TEST_TARGET=aarch64-linux-gnu
check_link absent absent
BLENDITY_LIBDIR="$work/arm" check_link present absent
export TEST_TARGET=x86_64-linux-gnu
check_link present present
BLENDITY_NO_LIBS=1 check_link absent absent
echo "Linux build target selection passed"
