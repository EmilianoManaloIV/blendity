#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Blendity's Linux packages (task 0076): a .tar.gz to unpack anywhere and an AppImage (one file: chmod +x, run), from
# the release build in dist/linux. The AppImage needs appimagetool: on PATH, or APPIMAGETOOL=/path/to/it; without it
# only the .tar.gz is made.
#   packaging/linux/make_packages.sh <version>
set -euo pipefail
cd "$(dirname "$0")/../.."
VERSION="${1:-$(cat VERSION)}"
DIST=dist/linux
OUT=dist/installers
[ -x "$DIST/Blendity" ] || { echo "no $DIST/Blendity: run ./build.sh release app first" >&2; exit 1; }
mkdir -p "$OUT" build/icons
python3 packaging/make_icon.py build/icons >/dev/null

# The tree both packages hold: the program, its libraries and data, the manual and the licences.
STAGE="build/package/blendity-$VERSION-linux-x86_64"
rm -rf "$STAGE"
mkdir -p "$STAGE/docs"
cp "$DIST/Blendity" "$STAGE/"
[ -x "$DIST/Blendity-full" ] && cp "$DIST/Blendity-full" "$STAGE/"  # (the full build Blendity hands over to, task 0080)
[ -d "$DIST/lib" ] && cp -a "$DIST/lib" "$STAGE/"
[ -d "$DIST/datafiles" ] && cp -a "$DIST/datafiles" "$STAGE/"
cp -a licenses "$STAGE/licenses"
cp LICENSE "$STAGE/"
cp docs/MANUAL.md "$STAGE/docs/"
cp build/icons/blendity.png "$STAGE/"
tar -C build/package -czf "$OUT/blendity-$VERSION-linux-x86_64.tar.gz" "blendity-$VERSION-linux-x86_64"
echo "made $OUT/blendity-$VERSION-linux-x86_64.tar.gz"

TOOL="${APPIMAGETOOL:-$(command -v appimagetool || true)}"
if [ -z "$TOOL" ]; then
  echo "no appimagetool: the .tar.gz only"
  exit 0
fi
APPDIR="build/package/Blendity.AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr"
cp -a "$STAGE" "$APPDIR/usr/blendity"
cp build/icons/blendity.png "$APPDIR/blendity.png"
cat > "$APPDIR/blendity.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Blendity
Comment=A Unity-style editor built on Blender's ideas
Exec=Blendity %f
Icon=blendity
Categories=Graphics;3DGraphics;
MimeType=application/x-blendity-scene;
Terminal=false
DESKTOP
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
# The AppImage's start: the program beside its libraries.
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/blendity/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/blendity/Blendity" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"
ARCH=x86_64 "$TOOL" --no-appstream "$APPDIR" "$OUT/Blendity-$VERSION-x86_64.AppImage"
echo "made $OUT/Blendity-$VERSION-x86_64.AppImage"
