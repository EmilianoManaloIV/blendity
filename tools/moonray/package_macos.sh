#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Task 0111 (ADR 0041): the archive Blendity downloads, from a MoonRay install (macOS, Apple silicon).
#   package_macos.sh <moonray install> <out dir>      (MOONRAY_TAG names the version)
# The same layout as package_linux.sh. Libraries are found through DYLD_LIBRARY_PATH, which the launcher sets: dyld
# looks there by file name first, for absolute and @rpath install names alike.
set -euo pipefail
INST=$1
OUT=$2
TAG=${MOONRAY_TAG:-unknown}
DEPS=/Applications/MoonRay/installs
SRC=/Applications/MoonRay/source/openmoonray
W=$(mktemp -d)
P=$W/moonray
mkdir -p "$P"/{bin,lib,rdl2dso,licenses,test} "$OUT"
cp "$INST/bin/moonray" "$P/bin/"
cp -a "$INST/rdl2dso/." "$P/rdl2dso/"
find "$INST/lib" "$INST/lib64" -maxdepth 1 -name '*.dylib' -exec cp -a {} "$P/lib/" \; 2>/dev/null || true
# Every library they load but the system's (/usr/lib, /System): until nothing new turns up.
find_lib() {  # an install name -> a file
  local n=$1 b
  b=$(basename "$n")
  for d in "$P/lib" "$INST/lib" "$DEPS/lib" "$DEPS/lib64"; do
    [ -e "$d/$b" ] && { echo "$d/$b"; return; }
  done
  [ -e "$n" ] && echo "$n"
}
while :; do
  added=0
  for f in "$P/bin/moonray" "$P"/rdl2dso/*.so* "$P"/rdl2dso/*.dylib "$P"/lib/*.dylib; do
    [ -f "$f" ] || continue
    otool -L "$f" | tail -n +2 | awk '{print $1}'
  done | sort -u >"$W/needed"
  while read -r n; do
    case "$n" in /usr/lib/*|/System/*) continue ;; esac
    b=$(basename "$n")
    [ -e "$P/lib/$b" ] && continue
    l=$(find_lib "$n")
    if [ -z "$l" ]; then
      echo "not found: $n" >&2
      continue
    fi
    cp -L "$l" "$P/lib/$b"
    chmod u+w "$P/lib/$b"
    added=1
  done <"$W/needed"
  [ "$added" = 1 ] || break
done
cat >"$P/bin/blendity-moonray" <<'EOF'
#!/bin/sh
# Runs MoonRay from this folder (Blendity's archive): its libraries and scene objects beside it.
root=$(cd "$(dirname "$0")/.." && pwd)
DYLD_LIBRARY_PATH="$root/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
RDL2_DSO_PATH="$root/rdl2dso"
REZ_MOONRAY_ROOT="$root"
export DYLD_LIBRARY_PATH RDL2_DSO_PATH REZ_MOONRAY_ROOT
exec "$root/bin/moonray" "$@"
EOF
chmod +x "$P/bin/blendity-moonray"
cp "$SRC/LICENSE" "$P/licenses/MoonRay-LICENSE"
[ -f "$SRC/NOTICE" ] && cp "$SRC/NOTICE" "$P/licenses/MoonRay-NOTICE"
[ -d "$DEPS/licenses" ] && cp -r "$DEPS/licenses/." "$P/licenses/"
cp "$SRC/testdata/rectangle.rdla" "$P/test/"
echo "$TAG" >"$P/VERSION"
# (Copied libraries lose their signatures' validity once touched: sign them again, ad hoc, as Apple silicon requires.)
codesign --force -s - "$P/bin/moonray" "$P"/lib/*.dylib "$P"/rdl2dso/* 2>/dev/null || true
name="blendity-moonray-macos-arm64-$TAG.tar.gz"
tar -C "$W" -czf "$OUT/$name" moonray
(cd "$OUT" && shasum -a 256 "$name" >"$name.sha256")
du -sh "$P" "$OUT/$name"
"$P/bin/blendity-moonray" -in "$P/test/rectangle.rdla" -out /tmp/rectangle.exr
ls -l /tmp/rectangle.exr
