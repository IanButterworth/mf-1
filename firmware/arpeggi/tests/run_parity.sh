#!/bin/sh
# Builds and runs tests/parity.cc: the firmware's C port of Arpeggi against MF-1's C++. Needs only the Mac's compiler.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

sh tools/gen_scenes.sh "$TMP"
for f in scenes.inc arp.c ArpeggiCore.h; do
  cmp -s "$TMP/$f" "dsp/$f" || { echo "FAIL dsp/$f is out of date: run tools/gen_scenes.sh"; exit 1; }
done
echo "ok   generated scenes and the copied lanes are up to date"

CFLAGS="-O2 -ffp-contract=off"
INC="-Idsp -I../iscream/dsp"
W="-Wall -Wextra -Wno-unused-parameter -Wdouble-promotion"
cc -std=c99 $CFLAGS $W $INC -c dsp/ap.c -o "$TMP/ap.o"
cc -std=c99 $CFLAGS $W $INC -c dsp/ap_player.c -o "$TMP/player.o"
cc -std=c99 $CFLAGS $W $INC -c dsp/arp.c -o "$TMP/arp.o"
cc -std=c99 $CFLAGS $W $INC -c ../iscream/dsp/fx.c -o "$TMP/fx.o"
# The desktop's player and settings, with the lanes from the one arp.c both share.
c++ -std=c++17 $CFLAGS -I"$ROOT/Sources/MF1Core" -I"$ROOT/Sources/MF1Core/include" -I"$ROOT/Sources/ArpeggiCore" \
  -I"$ROOT/Sources/ArpeggiCore/include" tests/parity.cc "$ROOT/Sources/MF1Core/fx.cc" \
  "$ROOT/Sources/ArpeggiCore/scenes.cc" "$ROOT/Sources/ArpeggiCore/sounds.cc" "$ROOT/Sources/ArpeggiCore/arpeggi.cc" \
  "$ROOT/Sources/ArpeggiCore/player.cc" "$TMP/ap.o" "$TMP/player.o" "$TMP/arp.o" "$TMP/fx.o" -o "$TMP/parity"
"$TMP/parity"
