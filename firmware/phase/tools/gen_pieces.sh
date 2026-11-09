#!/bin/sh
# Regenerates dsp/pieces.inc from MF-1's Phase sources, and copies the desktop's process.c and PhaseCore.h (the device
# build sees only firmware/), into OUT_DIR (default dsp).
# Run it after changing the pieces, the sounds or the processes; tests/run_parity.sh fails while they are out of date.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=${1:-dsp}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
c++ -std=c++17 -O1 -I"$ROOT/Sources/PhaseCore" -I"$ROOT/Sources/PhaseCore/include" \
  tools/gen_pieces.cc "$ROOT/Sources/PhaseCore/pieces.cc" "$ROOT/Sources/PhaseCore/sounds.cc" -o "$TMP/gen"
"$TMP/gen" > "$OUT/pieces.inc"
cp "$ROOT/Sources/PhaseCore/process.c" "$ROOT/Sources/PhaseCore/include/PhaseCore.h" "$OUT/"
