#!/bin/sh
# Builds MF-1.app in ./build from the Swift package.
# UNIVERSAL=1 builds for both Apple silicon and Intel, as for releases.
set -e
cd "$(dirname "$0")/.."
VERSION=$(cat VERSION)
ARCHS=""
[ "${UNIVERSAL:-0}" = 1 ] && ARCHS="--arch arm64 --arch x86_64"
swift build -c release $ARCHS --product MF1
BIN=$(swift build -c release $ARCHS --show-bin-path)
APP=build/MF-1.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN/MF1" "$APP/Contents/MacOS/MF-1"
cp -R "$BIN/MF1_MF1.bundle" "$APP/Contents/Resources/"
[ -f Resources/AppIcon.icns ] && cp Resources/AppIcon.icns "$APP/Contents/Resources/"
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
sed -i '' "s/VERSION_PLACEHOLDER/$VERSION/g" "$APP/Contents/Info.plist"
codesign --force --sign - "$APP" >/dev/null
echo "built $APP"
