/* Plays a short phrase through the firmware's engine (voices, mix and effects) into a 44.1 kHz mono WAV, so the
 * firmware's sound can be heard on a computer: render FLAVOR OUT.wav (FLAVOR 1..12). Built by tools/render.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iscream.h"
#include "synth.h"

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

int main(int argc, char **argv)
{
    /* C major arpeggio, then a held chord: note, start and length in blocks of 64 samples. */
    static const int phrase[][3] = {{60, 0, 150}, {64, 170, 150}, {67, 340, 150}, {72, 510, 150},
                                    {60, 700, 900}, {64, 700, 900}, {67, 700, 900}};
    const int blocks = 2400, n = (int)(sizeof phrase / sizeof phrase[0]);
    int32_t sundae[ISC_FIELDS];
    uint8_t voice[155];
    isc_fx_params_t fx;
    FILE *out;
    int b, i, k, flavor;
    if (argc < 3) {
        fprintf(stderr, "usage: render FLAVOR OUT.wav\n");
        return 2;
    }
    flavor = atoi(argv[1]) - 1;
    isc_synth_init();
    isc_default_sundae(flavor, sundae);
    isc_voice(sundae, voice);
    isc_fx(sundae, &fx);
    isc_synth_set_voice(voice, isc_flavor(flavor)->mono);
    isc_synth_set_fx(&fx);
    out = fopen(argv[2], "wb");
    if (!out)
        return 1;
    fwrite("RIFF", 1, 4, out);
    put32(out, 36 + (uint32_t)blocks * FM_N * 2);
    fwrite("WAVEfmt ", 1, 8, out);
    put32(out, 16);
    put16(out, 1);
    put16(out, 1);
    put32(out, 44100);
    put32(out, 44100 * 2);
    put16(out, 2);
    put16(out, 16);
    fwrite("data", 1, 4, out);
    put32(out, (uint32_t)blocks * FM_N * 2);
    for (b = 0; b < blocks; b++) {
        float x[FM_N];
        for (k = 0; k < n; k++) {
            if (phrase[k][1] == b)
                isc_synth_note_on(phrase[k][0], 100);
            if (phrase[k][1] + phrase[k][2] == b)
                isc_synth_note_off(phrase[k][0]);
        }
        isc_synth_block(x);
        for (i = 0; i < FM_N; i++) {
            float s = x[i] * 32767.f;
            int16_t v = (int16_t)(s > 32767.f ? 32767 : s < -32768.f ? -32768 : (int)s);
            put16(out, (uint16_t)v);
        }
    }
    fclose(out);
    printf("%s: %s\n", argv[2], isc_flavor(flavor)->title);
    return 0;
}
