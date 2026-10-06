#!/usr/bin/env bash
# Blendity - Linux / macOS build without CMake.
# Needs only a C++17 compiler (g++ 9+ or clang 10+) and, on Linux, the X11 headers
# (Debian/Ubuntu: sudo apt install g++ libx11-dev).
# Usage: ./build.sh [release|debug] [app|stress|tests|all]
set -euo pipefail
cd "$(dirname "$0")"

CONFIG="${1:-release}"
TARGET="${2:-all}"
OS="$(uname -s)"
CXX="${CXX:-c++}"
JOBS="$( (command -v nproc >/dev/null && nproc) || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

FLAGS="-Iextern/sky/include -std=c++17 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -pthread"
if [ "$CONFIG" = "debug" ]; then FLAGS="$FLAGS -O0 -g -DBL_DEBUG"; else FLAGS="$FLAGS -O2 -DNDEBUG"; fi

if [ "$OS" = "Darwin" ]; then
  PLATFORM_SRC="src/platform/platform_cocoa.mm"
  LIBS="-framework Cocoa -framework QuartzCore"
  OUT="dist/macos"
else
  PLATFORM_SRC="src/platform/platform_x11.cpp"
  LIBS="-lX11 -static-libstdc++ -static-libgcc"
  OUT="dist/linux"
fi

OBJ="build/obj_${CONFIG}_$(echo "$OS" | tr '[:upper:]' '[:lower:]')"
mkdir -p "$OBJ" "$OUT"

LIB_SRC="$(ls src/core/*.cpp src/image/*.cpp src/render/*.cpp src/scene/*.cpp src/editor/*.cpp src/research/*.cpp) extern/ufbx/ufbx.c extern/sky/source/sky_hosek.cpp $PLATFORM_SRC"

compile() {  # src -> object path (only rebuilds when the source or any header is newer)
  local src="$1"
  local obj="$OBJ/$(echo "$src" | tr '/' '_').o"
  local newest_header
  newest_header="$(ls -t src/*/*.h extern/*.hh | head -1)"
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ "$newest_header" -nt "$obj" ]; then
    local extra=""
    case "$src" in *.mm) extra="-fobjc-arc" ;; esac
    echo "  compiling $src"
    case "$src" in
      *.c) ${CC:-cc} -O2 -c "$src" -o "$obj" ;;  # ufbx is C99
      *) $CXX $FLAGS $extra -c "$src" -o "$obj" ;;
    esac
  fi
}
export -f compile
export CXX FLAGS OBJ

echo "[blendity] Compiling ($CONFIG, $OS, $JOBS jobs)..."
ALL_SRC="$LIB_SRC src/app/main.cpp stress/stress_main.cpp tests/test_main.cpp"
echo $ALL_SRC | tr ' ' '\n' | xargs -P "$JOBS" -I{} bash -c 'compile "$@"' _ {}

lib_objs() {
  for s in $LIB_SRC; do printf '%s ' "$OBJ/$(echo "$s" | tr '/' '_').o"; done
}
link() {  # main-source output-name
  echo "[blendity] Linking $2"
  $CXX $FLAGS $(lib_objs) "$OBJ/$(echo "$1" | tr '/' '_').o" -o "$OUT/$2" $LIBS
}

case "$TARGET" in
  app|all) link src/app/main.cpp Blendity ;;
esac
case "$TARGET" in
  stress|all) link stress/stress_main.cpp blendity_stress ;;
esac
case "$TARGET" in
  tests|all) link tests/test_main.cpp blendity_tests ;;
esac

if [ "$OS" = "Darwin" ] && { [ "$TARGET" = "app" ] || [ "$TARGET" = "all" ]; }; then
  # Wrap the editor in a minimal .app bundle so it can be double-clicked in Finder.
  APP="$OUT/Blendity.app/Contents"
  mkdir -p "$APP/MacOS"
  cp "$OUT/Blendity" "$APP/MacOS/Blendity"
  cat > "$APP/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>Blendity</string>
  <key>CFBundleIdentifier</key><string>org.blendity.editor</string>
  <key>CFBundleExecutable</key><string>Blendity</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.9.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
  echo "[blendity] Built $OUT/Blendity.app"
fi
echo "[blendity] Done. Binaries are in $OUT/"
