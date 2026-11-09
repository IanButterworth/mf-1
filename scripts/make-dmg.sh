#!/bin/sh
# Builds a universal MF-1.app and packs it into build/MF-1-<version>.dmg,
# with the usual drag-to-Applications window. The app carries the update feed.
# With SIGN_IDENTITY (a Developer ID Application identity) the app and DMG are signed with it; with
# NOTARY_KEY (an App Store Connect API key file), NOTARY_KEY_ID and NOTARY_ISSUER as well, the DMG is notarised
# and stapled. Without them the app is ad-hoc signed, as before.
set -e
cd "$(dirname "$0")/.."
VERSION=$(cat VERSION)
UNIVERSAL=1 UPDATES=1 scripts/build-app.sh
STAGE=build/dmg
RW=build/MF-1-rw.dmg
OUT=build/MF-1-$VERSION.dmg
rm -rf "$STAGE" "$RW" "$OUT"
mkdir -p "$STAGE/.background"
cp -R build/MF-1.app "$STAGE/"
ln -s /Applications "$STAGE/Applications"
swift scripts/make-dmg-background.swift "$STAGE/.background/background.png"
hdiutil create -quiet -volname "MF-1" -srcfolder "$STAGE" -fs HFS+ -format UDRW -ov "$RW"
DEV=$(hdiutil attach -readwrite -noverify -noautoopen "$RW" | awk '/Apple_HFS/ {print $1}')
sleep 1
osascript <<'APPLESCRIPT'
tell application "Finder"
  tell disk "MF-1"
    open
    set current view of container window to icon view
    set toolbar visible of container window to false
    set statusbar visible of container window to false
    set the bounds of container window to {200, 120, 800, 480}
    set opts to the icon view options of container window
    set arrangement of opts to not arranged
    set icon size of opts to 112
    set background picture of opts to file ".background:background.png"
    set position of item "MF-1.app" of container window to {150, 170}
    set position of item "Applications" of container window to {450, 170}
    update without registering applications
    delay 1
    close
  end tell
end tell
APPLESCRIPT
sync
hdiutil detach -quiet "$DEV"
hdiutil convert -quiet "$RW" -format UDZO -imagekey zlib-level=9 -o "$OUT"
rm -f "$RW"
rm -rf "$STAGE"
if [ -n "$SIGN_IDENTITY" ]; then
  codesign --force --timestamp --sign "$SIGN_IDENTITY" "$OUT"
  if [ -n "$NOTARY_KEY" ]; then
    xcrun notarytool submit "$OUT" --key "$NOTARY_KEY" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER" --wait
    xcrun stapler staple "$OUT"
    spctl --assess --type open --context context:primary-signature -v "$OUT"
  fi
fi
echo "built $OUT"
