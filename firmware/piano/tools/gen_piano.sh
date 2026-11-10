#!/bin/sh
# Copies the desktop's piano engine, its tables and voicings, and PianoCore.h (the device build sees only firmware/)
# into OUT_DIR (default dsp). Run it after changing Sources/PianoCore; tests/run_parity.sh fails while they differ.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=${1:-dsp}
cp "$ROOT/Sources/PianoCore/piano.c" "$ROOT/Sources/PianoCore/piano.h" "$ROOT/Sources/PianoCore/piano_tables.inc" \
  "$ROOT/Sources/PianoCore/voicings.c" "$ROOT/Sources/PianoCore/include/PianoCore.h" "$OUT/"
