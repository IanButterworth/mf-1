#!/bin/sh
# Builds MF-1.app in ./build from the Swift package.
# UNIVERSAL=1 builds for both Apple silicon and Intel, as for releases.
# UPDATES=1 adds the update feed, as for releases; local builds never offer to replace themselves. FEED_URL
# points it elsewhere, to test an update.
# SIGN_IDENTITY signs with that Developer ID and the hardened runtime, as notarisation needs; without it the
# app is ad-hoc signed.
set -e
cd "$(dirname "$0")/.."
VERSION=$(cat VERSION)
ARCHS=""
[ "${UNIVERSAL:-0}" = 1 ] && ARCHS="--arch arm64 --arch x86_64"
swift build -c release $ARCHS --product MF1
BIN=$(swift build -c release $ARCHS --show-bin-path)
APP=build/MF-1.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"
cp "$BIN/MF1" "$APP/Contents/MacOS/MF-1"
install_name_tool -add_rpath @executable_path/../Frameworks "$APP/Contents/MacOS/MF-1"
ditto "$BIN/Sparkle.framework" "$APP/Contents/Frameworks/Sparkle.framework"
cp -R "$BIN/MF1_MF1.bundle" "$APP/Contents/Resources/"
[ -f Resources/AppIcon.icns ] && cp Resources/AppIcon.icns "$APP/Contents/Resources/"
cp LICENSE THIRD_PARTY.md "$APP/Contents/Resources/"
cp -R LICENSES "$APP/Contents/Resources/"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>MF-1</string>
  <key>CFBundleDisplayName</key><string>MF-1</string>
  <key>CFBundleIdentifier</key><string>org.mf1.synth</string>
  <key>CFBundleExecutable</key><string>MF-1</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>VERSION_PLACEHOLDER</string>
  <key>CFBundleVersion</key><string>VERSION_PLACEHOLDER</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.music</string>
  <key>SUPublicEDKey</key><string>+NldF3ItGE1I6nuud9Ou6c+yW0Z/rxII2E6xIEGY1RQ=</string>
  FEED_PLACEHOLDER
  <key>CFBundleDocumentTypes</key>
  <array>
    <dict>
      <key>CFBundleTypeName</key><string>DX7 SysEx</string>
      <key>CFBundleTypeRole</key><string>Viewer</string>
      <key>CFBundleTypeExtensions</key><array><string>syx</string></array>
    </dict>
  </array>
</dict>
</plist>
PLIST
FEED=""
[ "${UPDATES:-0}" = 1 ] &&
  FEED="<key>SUFeedURL</key><string>${FEED_URL:-https://github.com/IanButterworth/mf-1/releases/latest/download/appcast.xml}</string>"
sed -i '' -e "s/VERSION_PLACEHOLDER/$VERSION/g" -e "s|FEED_PLACEHOLDER|$FEED|" "$APP/Contents/Info.plist"
if [ -n "$SIGN_IDENTITY" ]; then
  # Inside out, as Sparkle's documentation gives it: its helpers, then the framework, then the app.
  SP="$APP/Contents/Frameworks/Sparkle.framework/Versions/B"
  sign() { codesign --force --timestamp --options runtime --sign "$SIGN_IDENTITY" "$@"; }
  sign "$SP/XPCServices/Installer.xpc"
  sign --preserve-metadata=entitlements "$SP/XPCServices/Downloader.xpc"
  sign "$SP/Autoupdate"
  sign "$SP/Updater.app"
  sign "$APP/Contents/Frameworks/Sparkle.framework"
  sign "$APP"
else
  codesign --force --deep --sign - "$APP" >/dev/null
fi
echo "built $APP"
