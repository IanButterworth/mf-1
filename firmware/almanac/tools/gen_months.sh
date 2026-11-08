#!/bin/sh
# Regenerates dsp/months.inc from MF-1's Almanac sources, and copies the desktop's scene.c and AlmanacCore.h
# (the device build sees only firmware/), into OUT_DIR (default dsp).
# Run it after changing the months, skies or scene; tests/run_parity.sh fails while they are out of date.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=${1:-dsp}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
c++ -std=c++17 -O1 -I"$ROOT/Sources/AlmanacCore" -I"$ROOT/Sources/AlmanacCore/include" \
  tools/gen_months.cc "$ROOT/Sources/AlmanacCore/months.cc" -o "$TMP/gen"
"$TMP/gen" > "$OUT/months.inc"
cp "$ROOT/Sources/AlmanacCore/scene.c" "$ROOT/Sources/AlmanacCore/include/AlmanacCore.h" "$OUT/"
