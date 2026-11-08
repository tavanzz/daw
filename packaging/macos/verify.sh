#!/bin/bash
# Checks a release's disk image as a user's Mac would: the app's signature, and when it
# is signed, Developer ID, notarisation and stapled tickets, and Gatekeeper's verdict on
# a copy of the image downloaded from the internet (quarantined).
#
#   packaging/macos/verify.sh <DAW.dmg> signed|unsigned
set -euo pipefail
# (No "grep -q" after a pipe: it stops reading early, and pipefail then fails the pipe.)

dmg="$1"
mode="$2"

mount="$(mktemp -d)"
quarantined="$(mktemp -d)/$(basename "$dmg")"
cleanup() {
    hdiutil detach -quiet -force "$mount" 2>/dev/null || true
    rm -rf "$mount" "$(dirname "$quarantined")"
}
trap cleanup EXIT

# A copy marked as downloaded by a browser, as friends get it.
cp "$dmg" "$quarantined"
xattr -w com.apple.quarantine "0081;$(printf '%x' "$(date +%s)");Safari;" "$quarantined"
hdiutil attach -quiet -nobrowse -readonly -mountpoint "$mount" "$quarantined"
app="$(find "$mount" -maxdepth 1 -name '*.app' | head -1)"
[ -n "$app" ] || { echo "No app in $dmg" >&2; exit 1; }

echo "== Signature"
codesign --verify --deep --strict --verbose=2 "$app"
codesign -d --entitlements - --xml "$app" 2>/dev/null | plutil -p - || true
lipo -archs "$app/Contents/MacOS/$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$app/Contents/Info.plist")"

if [ "$mode" = "signed" ]; then
    echo "== Developer ID, notarisation, tickets"
    codesign -dv --verbose=2 "$app" 2>&1 | grep -E "Authority=Developer ID Application|TeamIdentifier|flags=.*runtime"
    xcrun stapler validate "$app"
    xcrun stapler validate "$dmg"
    spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
    echo "== Gatekeeper on the downloaded copy"
    spctl --assess --type execute --verbose=4 "$app" 2>&1 | tee /dev/stderr | grep "source=Notarized Developer ID" > /dev/null
else
    echo "== Unsigned build: ad-hoc signature only (Gatekeeper will ask friends to confirm)"
    codesign -dv "$app" 2>&1 | grep "Signature=adhoc" > /dev/null
fi
echo "Verified $dmg ($mode)"
