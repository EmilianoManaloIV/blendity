#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Blendity's macOS disk image (task 0076): Blendity.app (from ./build.sh release app) with its icon and version, and a
# link to Applications to drag it onto. Unsigned: the first time, macOS needs a go-ahead (README, Download).
#   packaging/macos/make_dmg.sh <version>
set -euo pipefail
cd "$(dirname "$0")/../.."
VERSION="${1:-$(cat VERSION)}"
APP=dist/macos/Blendity.app
OUT=dist/installers
[ -d "$APP" ] || { echo "no $APP: run ./build.sh release app first" >&2; exit 1; }
mkdir -p "$OUT" build/icons
python3 packaging/make_icon.py build/icons >/dev/null
iconutil -c icns build/icons/blendity.iconset -o build/icons/blendity.icns
mkdir -p "$APP/Contents/Resources" "$APP/Contents/Resources/docs"
cp build/icons/blendity.icns "$APP/Contents/Resources/"
cp docs/MANUAL.md "$APP/Contents/Resources/docs/"
rm -rf "$APP/Contents/Resources/licenses"
cp -R licenses "$APP/Contents/Resources/licenses"
cp LICENSE "$APP/Contents/Resources/"
plist="$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$plist"
/usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string blendity.icns" "$plist" 2>/dev/null ||
  /usr/libexec/PlistBuddy -c "Set :CFBundleIconFile blendity.icns" "$plist"
# An ad-hoc signature over the whole bundle (no certificate): an unsigned Apple-silicon app downloaded from the web can
# be reported as "damaged"; signed ad hoc, macOS offers Open Anyway instead (README, Download; task 0082). If it
# doesn't verify (the bundle holds data beside the programs), the build goes on and the README's
# quarantine line covers "damaged".
if codesign --force --deep --sign - "$APP" && codesign --verify --deep --strict "$APP"; then
  echo "signed ad hoc"
else
  echo "warning: the ad-hoc signature over the bundle did not verify (its programs keep their own)" >&2
fi
STAGE=build/package/dmg
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
rm -f "$OUT/Blendity-$VERSION-macos.dmg"
hdiutil create -volname "Blendity $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$OUT/Blendity-$VERSION-macos.dmg"
echo "made $OUT/Blendity-$VERSION-macos.dmg"
