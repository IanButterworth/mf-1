#!/bin/sh
# Regenerates dsp/dogs.inc from MF-1's Woof sources, and copies the desktop's scene.c and WoofCore.h (the device
# build sees only firmware/), into OUT_DIR (default dsp).
# Run it after changing the breeds or the scene; tests/run_parity.sh fails while they are out of date.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=${1:-dsp}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
c++ -std=c++17 -O1 -I"$ROOT/Sources/WoofCore" -I"$ROOT/Sources/WoofCore/include" \
  tools/gen_dogs.cc "$ROOT/Sources/WoofCore/dogs.cc" -o "$TMP/gen"
"$TMP/gen" > "$OUT/dogs.inc"
cp "$ROOT/Sources/WoofCore/scene.c" "$ROOT/Sources/WoofCore/include/WoofCore.h" "$OUT/"
