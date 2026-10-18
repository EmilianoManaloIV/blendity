#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Task 0111 (ADR 0041): the archive Blendity downloads, from a MoonRay install (Linux).
#   package_linux.sh <moonray install> <out dir>      (MOONRAY_TAG names the version)
# Layout, under moonray/:
#   bin/moonray          the renderer        bin/blendity-moonray   the launcher Blendity runs (sets the paths)
#   lib/                 every shared library it loads but glibc's (so it runs on any distro with glibc >= Rocky 9's)
#   rdl2dso/             MoonRay's scene objects (geometry, lights, materials), loaded at run time
#   licenses/            MoonRay's Apache-2.0 licence and each dependency's
#   test/rectangle.rdla  a scene to check the install with        VERSION
set -euo pipefail
INST=$1
OUT=$2
TAG=${MOONRAY_TAG:-unknown}
P=/pkg/moonray
rm -rf /pkg
mkdir -p "$P"/{bin,lib,rdl2dso,licenses,test} "$OUT"
cp "$INST/bin/moonray" "$P/bin/"
cp -a "$INST/rdl2dso/." "$P/rdl2dso/"
for d in lib lib64; do
  [ -d "$INST/$d" ] && find "$INST/$d" -maxdepth 1 -name '*.so*' -exec cp -a {} "$P/lib/" \;
done
export LD_LIBRARY_PATH="$P/lib:/opt/MoonRay/installs/lib:/opt/MoonRay/installs/lib64:${LD_LIBRARY_PATH:-}"
# Every library they load, but glibc's own (the host's must be used): until nothing new turns up.
skip='^(libc|libm|libpthread|libdl|librt|libresolv|libutil|ld-linux-x86-64|linux-vdso)\.so'
while :; do
  added=0
  for f in "$P/bin/moonray" "$P"/rdl2dso/*.so* "$P"/lib/*.so*; do
    [ -f "$f" ] || continue
    ldd "$f" 2>/dev/null | awk '/=> \//{print $3}'
  done | sort -u >/tmp/needed
  while read -r l; do
    b=$(basename "$l")
    [[ "$b" =~ $skip ]] && continue
    [ -e "$P/lib/$b" ] && continue
    cp -L "$l" "$P/lib/$b"
    added=1
  done </tmp/needed
  [ "$added" = 1 ] || break
done
missing=$(ldd "$P/bin/moonray" "$P"/rdl2dso/*.so* "$P"/lib/*.so* 2>&1 | grep "not found" | sort -u || true)
if [ -n "$missing" ]; then
  echo "$missing"
  echo "missing libraries" >&2
  exit 1
fi
cat >"$P/bin/blendity-moonray" <<'EOF'
#!/bin/sh
# Runs MoonRay from this folder (Blendity's archive): its libraries and scene objects beside it.
root=$(cd "$(dirname "$0")/.." && pwd)
LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
RDL2_DSO_PATH="$root/rdl2dso"
REZ_MOONRAY_ROOT="$root"
export LD_LIBRARY_PATH RDL2_DSO_PATH REZ_MOONRAY_ROOT
exec "$root/bin/moonray" "$@"
EOF
chmod +x "$P/bin/blendity-moonray"
# Licences: MoonRay's, the superbuild's dependencies', and the system packages' a library came from.
cp /src/LICENSE "$P/licenses/MoonRay-LICENSE"
[ -f /src/NOTICE ] && cp /src/NOTICE "$P/licenses/MoonRay-NOTICE"
[ -d /opt/MoonRay/installs/licenses ] && cp -r /opt/MoonRay/installs/licenses/. "$P/licenses/"
for l in "$P"/lib/*; do
  pkg=$(rpm -qf "/usr/lib64/$(basename "$l")" --qf '%{NAME}\n' 2>/dev/null || true)
  [ -n "$pkg" ] && [ -d "/usr/share/licenses/$pkg" ] && cp -r "/usr/share/licenses/$pkg" "$P/licenses/" 2>/dev/null || true
done
cp /src/testdata/rectangle.rdla "$P/test/"
echo "$TAG" >"$P/VERSION"
strip --strip-unneeded "$P/bin/moonray" "$P"/lib/*.so* "$P"/rdl2dso/*.so* 2>/dev/null || true
name="blendity-moonray-linux-x64-$TAG.tar.gz"
tar -C /pkg -czf "$OUT/$name" moonray
(cd "$OUT" && sha256sum "$name" >"$name.sha256")
du -sh "$P" "$OUT/$name"
"$P/bin/blendity-moonray" -in "$P/test/rectangle.rdla" -out /tmp/rectangle.exr
ls -l /tmp/rectangle.exr
