#!/bin/sh
# Builds and runs tests/parity.cc: the firmware's C port of Woof against MF-1's C++. Needs only the Mac's compiler.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

sh tools/gen_dogs.sh "$TMP"
for f in dogs.inc scene.c WoofCore.h; do
  cmp -s "$TMP/$f" "dsp/$f" || { echo "FAIL dsp/$f is out of date: run tools/gen_dogs.sh"; exit 1; }
done
echo "ok   generated dogs and the copied scene are up to date"

CFLAGS="-O2 -ffp-contract=off"
INC="-Idsp -I../iscream/dsp"
cc -std=c99 $CFLAGS -Wall -Wextra -Wno-unused-parameter -Wdouble-promotion $INC -c dsp/wf.c -o "$TMP/wf.o"
cc -std=c99 $CFLAGS -Wall -Wextra -Wno-unused-parameter -Wdouble-promotion $INC -c ../iscream/dsp/fx.c -o "$TMP/fx.o"
c++ -std=c++17 $CFLAGS -I"$ROOT/Sources/MF1Core" -I"$ROOT/Sources/MF1Core/include" -I"$ROOT/Sources/WoofCore" \
  -I"$ROOT/Sources/WoofCore/include" tests/parity.cc "$ROOT/Sources/MF1Core/fx.cc" \
  "$ROOT/Sources/WoofCore/dogs.cc" "$ROOT/Sources/WoofCore/woof.cc" "$TMP/wf.o" "$TMP/fx.o" -o "$TMP/parity"
"$TMP/parity"
