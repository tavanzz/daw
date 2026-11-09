#!/bin/bash
# Makes the Linux AppImage from a Release build: the app, its scanner and licences in an
# AppDir, the libraries every distribution may not have bundled by linuxdeploy (the
# system's ALSA, X11, fonts and libcurl stay the system's), packed by appimagetool with
# the static type-2 runtime (no libfuse2 needed).
#
#   packaging/linux/make-appimage.sh <folder with MICINSTATION, its scanner and Licences> <version> <output folder>
#
# Tools are downloaded at pinned versions and checked by SHA-256 into $TOOLS (default
# ~/.cache/daw-appimage).
set -euo pipefail

source_dir="$1"
version="$2"
output="$3"
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
tools="${TOOLS:-$HOME/.cache/daw-appimage}"
name=MICINSTATION
id=io.github.tavanzz.micinstation
mkdir -p "$tools" "$output"

fetch() { # <url> <sha256> <name>
    local file="$tools/$3"
    if [ ! -f "$file" ] || ! echo "$2  $file" | sha256sum -c --status; then
        curl -fsSL -o "$file" "$1"
        echo "$2  $file" | sha256sum -c
    fi
    chmod +x "$file"
}
fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
    c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d linuxdeploy-x86_64.AppImage
fetch https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage \
    ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 appimagetool-x86_64.AppImage
fetch https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 \
    2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d runtime-x86_64

appdir="$(mktemp -d)/$name.AppDir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications" "$appdir/usr/share/mime/packages" \
    "$appdir/usr/share/icons/hicolor/512x512/apps"
cp "$source_dir/$name" "$source_dir/$name Plugin Scanner" "$appdir/usr/bin/"
cp -R "$source_dir/Licences" "$appdir/usr/bin/Licences"
cp "$here/$id.desktop" "$appdir/usr/share/applications/"
cp "$here/$id.xml" "$appdir/usr/share/mime/packages/"
python3 - "$root/app/resources/AppIcon.png" "$appdir/usr/share/icons/hicolor/512x512/apps/$id.png" <<'PY'
# A 512 px copy of the icon, by halving the 1024 px one (no imaging library needed).
import struct, sys, zlib
data = open(sys.argv[1], "rb").read()
chunks, pos = {}, 8
idat = b""
while pos < len(data):
    length, kind = struct.unpack(">I4s", data[pos:pos + 8])
    body = data[pos + 8:pos + 8 + length]
    if kind == b"IHDR": width, height = struct.unpack(">II", body[:8])
    if kind == b"IDAT": idat += body
    pos += 12 + length
raw = zlib.decompress(idat)
stride = width * 4
rows = [raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)] for y in range(height)]  # filter 0 only
out = bytearray()
for y in range(0, height, 2):
    out.append(0)
    a, b = rows[y], rows[y + 1]
    for x in range(0, stride, 8):
        for c in range(4):
            out.append((a[x + c] + a[x + 4 + c] + b[x + c] + b[x + 4 + c] + 2) // 4)
def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width // 2, height // 2, 8, 6, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(bytes(out), 9)) + chunk(b"IEND", b"")
open(sys.argv[2], "wb").write(png)
PY

# Libraries: linuxdeploy copies what ldd finds, minus its list of system libraries.
APPIMAGE_EXTRACT_AND_RUN=1 "$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" \
    --executable "$appdir/usr/bin/$name" --executable "$appdir/usr/bin/$name Plugin Scanner" \
    --desktop-file "$appdir/usr/share/applications/$id.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/512x512/apps/$id.png"

image="$output/$name-$version-Linux-x86_64.AppImage"
rm -f "$image"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$tools/appimagetool-x86_64.AppImage" --no-appstream \
    --runtime-file "$tools/runtime-x86_64" "$appdir" "$image"
chmod +x "$image"
echo "Made $image"
