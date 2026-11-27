#!/bin/sh
# Copies the desktop's songs, game, sounds and pictures, and RhymesCore.h (the device build sees only firmware/), into
# OUT_DIR (default dsp). Run it after changing Sources/RhymesCore; tests/run_parity.sh fails while they differ.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=${1:-dsp}
for f in songs.c game.c voices.c scene.c internal.h; do cp "$ROOT/Sources/RhymesCore/$f" "$OUT/"; done
cp "$ROOT/Sources/RhymesCore/include/RhymesCore.h" "$OUT/"
