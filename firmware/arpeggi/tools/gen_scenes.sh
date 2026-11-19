#!/bin/sh
# Regenerates dsp/scenes.inc from MF-1's Arpeggi sources, and copies the desktop's arp.c and ArpeggiCore.h (the
# device build sees only firmware/), into OUT_DIR (default dsp).
# Run it after changing the scenes, the sounds or the lanes; tests/run_parity.sh fails while they are out of date.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=${1:-dsp}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
c++ -std=c++17 -O1 -I"$ROOT/Sources/ArpeggiCore" -I"$ROOT/Sources/ArpeggiCore/include" \
  tools/gen_scenes.cc "$ROOT/Sources/ArpeggiCore/scenes.cc" "$ROOT/Sources/ArpeggiCore/sounds.cc" -o "$TMP/gen"
"$TMP/gen" > "$OUT/scenes.inc"
cp "$ROOT/Sources/ArpeggiCore/arp.c" "$ROOT/Sources/ArpeggiCore/include/ArpeggiCore.h" "$OUT/"
