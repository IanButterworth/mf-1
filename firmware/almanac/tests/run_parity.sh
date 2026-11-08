#!/bin/sh
# Builds and runs tests/parity.cc: the firmware's C port of Almanac against MF-1's C++. Needs only the Mac's compiler.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

sh tools/gen_months.sh "$TMP"
for f in months.inc scene.c AlmanacCore.h; do
  cmp -s "$TMP/$f" "dsp/$f" || { echo "FAIL dsp/$f is out of date: run tools/gen_months.sh"; exit 1; }
done
echo "ok   generated months and the copied scene are up to date"

CFLAGS="-O2 -ffp-contract=off"
INC="-Idsp -I../iscream/dsp"
cc -std=c99 $CFLAGS -Wall -Wextra -Wno-unused-parameter -Wdouble-promotion $INC -c dsp/alm.c -o "$TMP/alm.o"
cc -std=c99 $CFLAGS -Wall -Wextra -Wno-unused-parameter -Wdouble-promotion $INC -c ../iscream/dsp/fx.c -o "$TMP/fx.o"
c++ -std=c++17 $CFLAGS -I"$ROOT/Sources/MF1Core" -I"$ROOT/Sources/MF1Core/include" -I"$ROOT/Sources/AlmanacCore" \
  -I"$ROOT/Sources/AlmanacCore/include" tests/parity.cc "$ROOT/Sources/MF1Core/fx.cc" \
  "$ROOT/Sources/AlmanacCore/months.cc" "$ROOT/Sources/AlmanacCore/almanac.cc" "$TMP/alm.o" "$TMP/fx.o" -o "$TMP/parity"
"$TMP/parity"
