#!/bin/sh
# Writes build/appcast.xml, the update feed for one release: the DMG's EdDSA signature and the release notes.
# Usage: scripts/make-appcast.sh build/MF-1-<version>.dmg NOTES.md (after make-dmg.sh, which leaves build/MF-1.app)
# The private key is read from SPARKLE_PRIVATE_KEY, or from the login keychain when that is unset.
# The feed goes on the GitHub release beside the DMG, where the app looks for it.
set -e
cd "$(dirname "$0")/.."
DMG=$1
NOTES=$2
VERSION=$(cat VERSION)
SIGN_UPDATE=.build/artifacts/sparkle/Sparkle/bin/sign_update
[ -x "$SIGN_UPDATE" ] || swift package resolve
if [ -n "$SPARKLE_PRIVATE_KEY" ]; then
  ATTRS=$(printf '%s' "$SPARKLE_PRIVATE_KEY" | "$SIGN_UPDATE" --ed-key-file - "$DMG")
else
  ATTRS=$("$SIGN_UPDATE" --account mf-1 "$DMG")
fi
# The signature must check against the public key the app carries, or every update would be refused.
SIG=$(printf '%s' "$ATTRS" | sed -n 's/.*sparkle:edSignature="\([^"]*\)".*/\1/p')
swift scripts/verify-update.swift build/MF-1.app "$DMG" "$SIG"
URL="https://github.com/IanButterworth/mf-1/releases/download/v$VERSION/$(basename "$DMG")"
# CDATA cannot hold "]]>", so split any in the notes.
BODY=$(sed 's/]]>/]]]]><![CDATA[>/g' "$NOTES")
cat > build/appcast.xml <<XML
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>MF-1</title>
    <link>https://github.com/IanButterworth/mf-1</link>
    <item>
      <title>MF-1 $VERSION</title>
      <pubDate>$(LC_ALL=C date -u '+%a, %d %b %Y %H:%M:%S +0000')</pubDate>
      <sparkle:version>$VERSION</sparkle:version>
      <sparkle:shortVersionString>$VERSION</sparkle:shortVersionString>
      <sparkle:minimumSystemVersion>14.0</sparkle:minimumSystemVersion>
      <sparkle:fullReleaseNotesLink>https://github.com/IanButterworth/mf-1/releases</sparkle:fullReleaseNotesLink>
      <description sparkle:format="markdown"><![CDATA[$BODY]]></description>
      <enclosure url="$URL" type="application/x-apple-diskimage" $ATTRS/>
    </item>
  </channel>
</rss>
XML
xmllint --noout build/appcast.xml
echo "built build/appcast.xml"
