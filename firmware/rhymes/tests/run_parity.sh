#!/bin/sh
# The firmware's copy of Rhymes against MF-1's: the copied files must match the desktop's, and the copy, built as the
# device builds it (C99), must build without warnings and give the same tunes, voices, effects, game, lights and
# pictures however it is optimised, so nothing in it depends on the compiler's choices. Needs only the Mac's compiler.
set -e
cd "$(dirname "$0")/.."
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

sh tools/gen_rhymes.sh "$TMP"
for f in songs.c game.c voices.c scene.c internal.h RhymesCore.h; do
  cmp -s "$TMP/$f" "dsp/$f" || { echo "FAIL dsp/$f is out of date: run tools/gen_rhymes.sh"; exit 1; }
done
echo "ok   the copied songs, game, sounds and pictures are up to date"

W="-Wall -Wextra -Wno-unused-parameter -Wdouble-promotion -Werror"
for f in songs game voices scene; do cc -std=c99 -O2 $W -Idsp -c "dsp/$f.c" -o "$TMP/$f.o"; done
echo "ok   the copy builds as C99 with no double promotion"
for O in -O0 -O2; do
  cc -std=c99 $O $W -Idsp dsp/songs.c dsp/game.c dsp/voices.c dsp/scene.c tests/parity.c -o "$TMP/parity$O"
  "$TMP/parity$O" > "$TMP/out$O" || { cat "$TMP/out$O"; exit 1; }
done
cmp -s "$TMP/out-O0" "$TMP/out-O2" || { echo "FAIL -O0 and -O2 differ:"; cat "$TMP/out-O0" "$TMP/out-O2"; exit 1; }
echo "ok   every song plays, lights and draws the same unoptimised and optimised: $(cat "$TMP/out-O2")"
echo "all passed"
