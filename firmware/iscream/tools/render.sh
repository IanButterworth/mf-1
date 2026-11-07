#!/bin/sh
# Renders a phrase on one flavor through the firmware's engine: tools/render.sh FLAVOR OUT.wav (FLAVOR 1..12).
set -e
cd "$(dirname "$0")/.."
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cc -std=c99 -O2 -ffp-contract=off -Wall -Wextra -Wno-unused-parameter -Wdouble-promotion -Idsp \
  tools/render.c dsp/fm.c dsp/fx.c dsp/iscream.c dsp/synth.c -o "$TMP/render"
"$TMP/render" "$@"
