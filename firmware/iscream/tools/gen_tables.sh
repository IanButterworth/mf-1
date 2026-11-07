#!/bin/sh
# Regenerates dsp/fm_tables.inc and dsp/flavors.inc from MF-1's sources, into OUT_DIR (default dsp).
# Run it after changing the flavors or the msfa tables; tests/run_parity.sh fails while they are out of date.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
OUT=${1:-dsp}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
c++ -std=c++17 -O1 -I"$ROOT/Sources/MF1Core" -I"$ROOT/Sources/IScreamCore" -I"$ROOT/Sources/IScreamCore/include" \
  tools/gen_tables.cc "$ROOT/Sources/MF1Core/dsp/fm_dsp.cc" "$ROOT/Sources/IScreamCore/flavors.cc" -o "$TMP/gen"
"$TMP/gen" fm > "$OUT/fm_tables.inc"
"$TMP/gen" flavors > "$OUT/flavors.inc"
