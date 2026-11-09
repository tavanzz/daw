#!/bin/bash
# Sends a file (.zip of the app, or the .dmg) to Apple's notary service, waits for the
# verdict and staples the ticket to what can carry one (.app, .dmg).
#
#   packaging/macos/notarize.sh <file> [<app or dmg to staple>]
#
# Needs an App Store Connect API key: MACOS_NOTARY_KEY_FILE (the .p8), MACOS_NOTARY_KEY_ID,
# MACOS_NOTARY_ISSUER_ID.
set -euo pipefail

file="$1"
staple="${2:-$1}"
: "${MACOS_NOTARY_KEY_FILE:?}" "${MACOS_NOTARY_KEY_ID:?}" "${MACOS_NOTARY_ISSUER_ID:?}"

result="$(xcrun notarytool submit "$file" --key "$MACOS_NOTARY_KEY_FILE" --key-id "$MACOS_NOTARY_KEY_ID" \
    --issuer "$MACOS_NOTARY_ISSUER_ID" --wait --timeout 45m --output-format json)"
echo "$result"
status="$(echo "$result" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("status",""))')"
if [ "$status" != "Accepted" ]; then
    id="$(echo "$result" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("id",""))')"
    if [ -n "$id" ]; then
        xcrun notarytool log "$id" --key "$MACOS_NOTARY_KEY_FILE" --key-id "$MACOS_NOTARY_KEY_ID" \
            --issuer "$MACOS_NOTARY_ISSUER_ID" || true
    fi
    echo "Notarisation failed: $status" >&2
    exit 1
fi

xcrun stapler staple "$staple"
xcrun stapler validate "$staple"
