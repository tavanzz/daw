#!/bin/bash
# Signs the app bundle from the inside out, with the hardened runtime and the minimal
# entitlements (packaging/macos/*.entitlements).
#
#   packaging/macos/sign.sh <MICINSTATION.app> <identity>
#
# identity: a "Developer ID Application: ..." identity in the keychain, or "-" for an
# ad-hoc signature (builds without the Apple secrets: they run, but Gatekeeper asks).
set -euo pipefail

app="$1"
identity="$2"
here="$(cd "$(dirname "$0")" && pwd)"

if [ "$identity" = "-" ]; then
    timestamp="--timestamp=none"
else
    timestamp="--timestamp"
fi

sign() { # <path> <entitlements or empty>
    local args=(--force --options runtime "$timestamp" --sign "$identity")
    if [ -n "$2" ]; then
        args+=(--entitlements "$2")
    fi
    codesign "${args[@]}" "$1"
}

# Inside first: every helper program next to the app's executable.
for program in "$app"/Contents/MacOS/*; do
    if [ "$(basename "$program")" != "$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$app/Contents/Info.plist")" ]; then
        sign "$program" "$here/Scanner.entitlements"
    fi
done
sign "$app" "$here/App.entitlements"

codesign --verify --deep --strict --verbose=2 "$app"
echo "Signed $app ($([ "$identity" = "-" ] && echo ad-hoc || echo "$identity"))"
