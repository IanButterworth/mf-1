#!/bin/sh
# Builds and runs tests/parity.cc: the firmware's C engine against MF-1's C++ engine. Needs only the Mac's compiler.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

sh tools/gen_tables.sh "$TMP"
for f in fm_tables.inc flavors.inc; do
  cmp -s "$TMP/$f" "dsp/$f" || { echo "FAIL dsp/$f is out of date: run tools/gen_tables.sh"; exit 1; }
done
echo "ok   generated tables are up to date"

CFLAGS="-O2 -ffp-contract=off"
for f in fm fx iscream; do
  cc -std=c99 $CFLAGS -Wall -Wextra -Wno-unused-parameter -Wdouble-promotion -Idsp -c dsp/$f.c -o "$TMP/$f.o"
done
c++ -std=c++17 $CFLAGS -I"$ROOT/Sources/MF1Core" -I"$ROOT/Sources/MF1Core/include" -I"$ROOT/Sources/IScreamCore/include" \
  tests/parity.cc "$ROOT/Sources/MF1Core/dsp/fm_dsp.cc" "$ROOT/Sources/MF1Core/dsp/dx7_voice.cc" \
  "$ROOT/Sources/MF1Core/fx.cc" "$ROOT/Sources/IScreamCore/flavors.cc" "$ROOT/Sources/IScreamCore/iscream.cc" \
  -I"$ROOT/Sources/IScreamCore" "$TMP/fm.o" "$TMP/fx.o" "$TMP/iscream.o" -o "$TMP/parity"
"$TMP/parity" "$ROOT/Sources/MF1/Resources/Banks"
