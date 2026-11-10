#!/bin/sh
# The firmware's copy of the piano against MF-1's: the copied files must match the desktop's, and the copy, built as
# the device builds it (C99, no fused multiply-adds), must build without warnings and play the same samples however
# it is optimised, so nothing in it depends on the compiler's choices. Needs only the Mac's compiler.
set -e
cd "$(dirname "$0")/.."
ROOT=../..
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

sh tools/gen_piano.sh "$TMP"
for f in piano.c piano.h piano_tables.inc voicings.c PianoCore.h; do
  cmp -s "$TMP/$f" "dsp/$f" || { echo "FAIL dsp/$f is out of date: run tools/gen_piano.sh"; exit 1; }
done
echo "ok   the copied engine, tables and voicings are up to date"

CFLAGS="-O2 -ffp-contract=off"
W="-Wall -Wextra -Wno-unused-parameter -Wdouble-promotion -Werror"
cc -std=c99 $CFLAGS $W -Idsp -c dsp/piano.c -o "$TMP/piano.o"
cc -std=c99 $CFLAGS $W -Idsp -c dsp/voicings.c -o "$TMP/voicings.o"
echo "ok   the copy builds as C99 with no double promotion"
for O in -O0 -O2; do
  cc -std=c99 $O -ffp-contract=off $W -Idsp dsp/piano.c dsp/voicings.c tests/parity.c -o "$TMP/parity$O"
  "$TMP/parity$O" > "$TMP/out$O" || { cat "$TMP/out$O"; exit 1; }
done
cmp -s "$TMP/out-O0" "$TMP/out-O2" || { echo "FAIL -O0 and -O2 differ:"; cat "$TMP/out-O0" "$TMP/out-O2"; exit 1; }
echo "ok   every voicing plays the same samples unoptimised and optimised: $(cat "$TMP/out-O2")"
echo "all passed"
