#!/bin/bash
# Puts the app in a compressed disk image with a link to /Applications, so that
# installing is a drag; signs the image when an identity is given.
#
#   packaging/macos/make-dmg.sh <DAW.app> <output.dmg> <volume name> [identity]
set -euo pipefail

app="$1"
dmg="$2"
volume="$3"
identity="${4:-}"

staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT
ditto "$app" "$staging/$(basename "$app")"
ln -s /Applications "$staging/Applications"

rm -f "$dmg"
hdiutil create -quiet -volname "$volume" -srcfolder "$staging" -fs HFS+ -format UDZO -ov "$dmg"

if [ -n "$identity" ] && [ "$identity" != "-" ]; then
    codesign --force --timestamp --sign "$identity" "$dmg"
    codesign --verify --verbose=2 "$dmg"
fi
echo "Made $dmg"
