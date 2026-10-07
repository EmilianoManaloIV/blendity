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

# --- Blender's prebuilt libraries (optional) -----------------------------------
# Used when ../blender/lib/<platform> exists (Blender's own layout, from
# projects.blender.org/blender/lib-linux_x64 or lib-macos_arm64). Otherwise the
# dependency-free versions are built. BLENDITY_NO_LIBS=1 forces that build.
if [ "$OS" = "Darwin" ]; then LIBPLAT=macos_arm64; SOEXT=dylib; else LIBPLAT=linux_x64; SOEXT=so; fi
LIBDIR="${BLENDITY_LIBDIR:-../blender/lib/$LIBPLAT}"
USE_LIBS=0
LIBFLAGS=""
LIBLINK=""
LIBDIRS=""
addlib() {  # folder define "include dirs" "libraries (lib/ relative files)"
  [ -d "$LIBDIR/$1" ] || return 0
  LIBFLAGS="$LIBFLAGS -D$2"
  for i in $3; do LIBFLAGS="$LIBFLAGS -isystem $LIBDIR/$i"; done
  for l in $4; do LIBLINK="$LIBLINK $LIBDIR/$l"; done
  LIBDIRS="$LIBDIRS $1"
}
if [ -z "${BLENDITY_NO_LIBS:-}" ] && [ -d "$LIBDIR/tbb/include" ]; then
  USE_LIBS=1
  addlib tbb              BL_WITH_TBB        "tbb/include"                    "tbb/lib/libtbb.$SOEXT"
  addlib embree           BL_WITH_EMBREE     "embree/include"                 "embree/lib/libembree4.$SOEXT"
  addlib openimagedenoise BL_WITH_OIDN       "openimagedenoise/include"       "openimagedenoise/lib/libOpenImageDenoise.$SOEXT"
  addlib eigen            BL_WITH_EIGEN      "eigen/include/eigen3"           ""
  addlib opensubdiv       BL_WITH_OPENSUBDIV "opensubdiv/include"             "opensubdiv/lib/libosdCPU.$SOEXT"
  addlib openexr          BL_WITH_OPENEXR    "openexr/include openexr/include/OpenEXR imath/include imath/include/Imath" \
                                             "openexr/lib/libOpenEXR.$SOEXT openexr/lib/libOpenEXRCore.$SOEXT openexr/lib/libIex.$SOEXT openexr/lib/libIlmThread.$SOEXT imath/lib/libImath.$SOEXT"
  addlib opencolorio      BL_WITH_OCIO       "opencolorio/include"            "opencolorio/lib/libOpenColorIO.$SOEXT"
  addlib manifold         BL_WITH_MANIFOLD   "manifold/include"               "manifold/lib/libmanifold.a"
  addlib meshoptimizer    BL_WITH_MESHOPT    "meshoptimizer/include"          "meshoptimizer/lib/libmeshoptimizer.$SOEXT"
  addlib openpgl          BL_WITH_OPENPGL    "openpgl/include"                "openpgl/lib/libopenpgl.a"
  addlib jolt             BL_WITH_JOLT       "jolt/include"                   "jolt/lib/libJolt.$SOEXT"
  addlib jpeg             BL_WITH_LIBJPEG    "jpeg/include"                   "jpeg/lib/libjpeg.a"
  addlib png              BL_WITH_LIBPNG     "png/include zlib/include"       "png/lib/libpng.a zlib/lib/libz.a"
  addlib zstd             BL_WITH_ZSTD       "zstd/include"                   "zstd/lib/libzstd.a"
  # Static libraries (manifold, openpgl) use TBB: list it again after them.
  LIBLINK="$LIBLINK $LIBDIR/tbb/lib/libtbb.$SOEXT"
  # ABI settings the libraries were built with (blender/build_files/build_environment/cmake).
  # (Jolt only enables JPH_FLOATING_POINT_EXCEPTIONS_ENABLED for MSVC builds; a
  # mismatch makes its version check abort at start-up - see JoltConfig.cmake.)
  LIBFLAGS="$LIBFLAGS -DMANIFOLD_PAR=1 -DJPH_SHARED_LIBRARY -DJPH_DOUBLE_PRECISION"
  LIBFLAGS="$LIBFLAGS -DJPH_CROSS_PLATFORM_DETERMINISTIC -DJPH_USE_CPU_COMPUTE -DJPH_OBJECT_STREAM"
  [ "$OS" = "Darwin" ] || LIBFLAGS="$LIBFLAGS -DJPH_USE_SSE4_1 -DJPH_USE_SSE4_2 -msse4.2 -mpopcnt"
  FLAGS="$FLAGS $LIBFLAGS"
  # The libraries link the shared C++ runtime, so the executables must too; they
  # find the libraries in ./lib next to themselves.
  LIBS="${LIBS/ -static-libstdc++ -static-libgcc/} $LIBLINK -Wl,-rpath,\$ORIGIN/lib"
  [ "$OS" = "Darwin" ] && LIBS="${LIBS/\$ORIGIN/@executable_path}"
  # Transitive runtime libraries for the linker (OpenEXR -> OpenJPH, Embree -> SYCL runtime).
  [ "$OS" = "Darwin" ] || LIBS="$LIBS -Wl,-rpath-link,$LIBDIR/openjph/lib:$LIBDIR/dpcpp/lib:$LIBDIR/tbb/lib:$LIBDIR/imath/lib"
  echo "[blendity] Using Blender libraries from $LIBDIR:$LIBDIRS"
else
  echo "[blendity] Dependency-free build (no $LIBDIR)"
fi

OBJ="build/obj_${CONFIG}_$(echo "$OS" | tr '[:upper:]' '[:lower:]')$([ "$USE_LIBS" = 1 ] && echo _libs)"
mkdir -p "$OBJ" "$OUT"
# License texts and notices travel with the binaries (licenses/README.md).
rm -rf "$OUT/licenses" && cp -r licenses "$OUT/licenses"
# Objects are only rebuilt when sources change, so a change of compiler flags
# (e.g. a library's ABI defines) must start the object folder afresh.
if [ "$(cat "$OBJ/flags.txt" 2>/dev/null)" != "$CXX $FLAGS" ]; then
  rm -f "$OBJ"/*.o
  echo "$CXX $FLAGS" > "$OBJ/flags.txt"
fi

LIB_SRC="$(ls src/core/*.cpp src/image/*.cpp src/render/*.cpp src/scene/*.cpp src/editor/*.cpp src/research/*.cpp src/deps/*.cpp 2>/dev/null) extern/ufbx/ufbx.c extern/sky/source/sky_hosek.cpp $PLATFORM_SRC"

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

if [ "$USE_LIBS" = 1 ]; then
  # Ship the shared libraries (release only: no debug, GPU-device or Python files).
  mkdir -p "$OUT/lib"
  for d in $LIBDIRS imath openjph dpcpp; do
    [ -d "$LIBDIR/$d/lib" ] || continue
    find "$LIBDIR/$d/lib" -maxdepth 1 -name "*.$SOEXT*" ! -name "*_d.*" ! -name "*debug*" ! -name "*_cuda*" \
      ! -name "*_hip*" ! -name "*_sycl*" -exec cp -a {} "$OUT/lib/" \;
  done
  # Blender's colour management config (OpenColorIO views: AgX, Filmic, ...).
  OCIO_CFG="../blender/release/datafiles/colormanagement"
  if [ -d "$LIBDIR/opencolorio" ] && [ -f "$OCIO_CFG/config.ocio" ]; then
    mkdir -p "$OUT/datafiles"
    cp -r "$OCIO_CFG" "$OUT/datafiles/"
  fi
fi

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
