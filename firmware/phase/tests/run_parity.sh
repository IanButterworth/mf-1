#!/bin/sh
# Builds and runs tests/parity.cc: the firmware's C port of Phase against MF-1's C++. Needs only the Mac's compiler.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

sh tools/gen_pieces.sh "$TMP"
for f in pieces.inc process.c PhaseCore.h; do
  cmp -s "$TMP/$f" "dsp/$f" || { echo "FAIL dsp/$f is out of date: run tools/gen_pieces.sh"; exit 1; }
done
echo "ok   generated pieces and the copied processes are up to date"

CFLAGS="-O2 -ffp-contract=off"
INC="-Idsp -I../iscream/dsp"
W="-Wall -Wextra -Wno-unused-parameter -Wdouble-promotion"
cc -std=c99 $CFLAGS $W $INC -c dsp/ph.c -o "$TMP/ph.o"
cc -std=c99 $CFLAGS $W $INC -c dsp/player.c -o "$TMP/player.o"
cc -std=c99 $CFLAGS $W $INC -c dsp/process.c -o "$TMP/process.o"
cc -std=c99 $CFLAGS $W $INC -c ../iscream/dsp/fx.c -o "$TMP/fx.o"
# the device's 64-bit divisions, under other names, against the Mac's own
cc -std=c99 $CFLAGS $W -D__divdi3=ph_test_divdi3 -D__moddi3=ph_test_moddi3 -c dsp/div64.c -o "$TMP/div64.o"
c++ -std=c++17 $CFLAGS -I"$ROOT/Sources/MF1Core" -I"$ROOT/Sources/MF1Core/include" -I"$ROOT/Sources/PhaseCore" \
  -I"$ROOT/Sources/PhaseCore/include" tests/parity.cc "$ROOT/Sources/MF1Core/fx.cc" \
  "$ROOT/Sources/PhaseCore/pieces.cc" "$ROOT/Sources/PhaseCore/sounds.cc" "$ROOT/Sources/PhaseCore/phase.cc" \
  "$ROOT/Sources/PhaseCore/player.cc" "$TMP/ph.o" "$TMP/player.o" "$TMP/process.o" "$TMP/fx.o" "$TMP/div64.o" -o "$TMP/parity"
"$TMP/parity"
