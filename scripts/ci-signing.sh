#!/bin/bash
# CI only: puts the Developer ID certificate and the notary key from the secrets where make-dmg.sh finds them.
# Without MACOS_CERT_P12 it does nothing, and the DMG is ad-hoc signed.
set -e
[ -n "$MACOS_CERT_P12" ] || { echo "no Developer ID secret: the DMG will be ad-hoc signed"; exit 0; }
KC=$RUNNER_TEMP/signing.keychain-db
KC_PASS=$(uuidgen)
printf '%s' "$MACOS_CERT_P12" | base64 --decode > "$RUNNER_TEMP/cert.p12"
security create-keychain -p "$KC_PASS" "$KC"
security set-keychain-settings -lut 21600 "$KC"
security unlock-keychain -p "$KC_PASS" "$KC"
security import "$RUNNER_TEMP/cert.p12" -P "$MACOS_CERT_PASSWORD" -A -t cert -f pkcs12 -k "$KC"
rm "$RUNNER_TEMP/cert.p12"
security set-key-partition-list -S apple-tool:,apple: -k "$KC_PASS" "$KC" >/dev/null
security list-keychains -d user -s "$KC" $(security list-keychains -d user | tr -d '"')
# Apple's Developer ID G2 intermediate, which the certificate needs to be valid and a runner may not have.
curl -fsSL -o "$RUNNER_TEMP/DeveloperIDG2CA.cer" https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer
shasum -a 256 "$RUNNER_TEMP/DeveloperIDG2CA.cer" |
  grep -q '^f16cd3c54c7f83cea4bf1a3e6a0819c8aaa8e4a1528fd144715f350643d2df3a ' ||
  { echo "Apple's G2 intermediate did not match its known fingerprint" >&2; exit 1; }
security import "$RUNNER_TEMP/DeveloperIDG2CA.cer" -k "$KC" || true
ID=$(security find-identity -v -p codesigning "$KC" | sed -n 's/.*"\(Developer ID Application: .*\)"/\1/p' | head -1)
if [ -z "$ID" ]; then
  echo "MACOS_CERT_P12 holds no valid Developer ID Application identity; it has:" >&2
  security find-identity -p codesigning "$KC" >&2
  exit 1
fi
echo "signing as $ID"
echo "SIGN_IDENTITY=$ID" >> "$GITHUB_ENV"
if [ -n "$NOTARY_KEY_P8" ]; then
  printf '%s' "$NOTARY_KEY_P8" | base64 --decode > "$RUNNER_TEMP/notary.p8"
  echo "NOTARY_KEY=$RUNNER_TEMP/notary.p8" >> "$GITHUB_ENV"
fi
