/* Plays a phrase through every voicing, with the pedal, at the FM-1's rate, and prints a checksum of the samples and
 * the peak. run_parity.sh builds it at -O0 and at -O2 and needs the two to agree bit for bit. */
#include <stdio.h>
#include <string.h>
#include "PianoCore.h"
#include "piano.h"

static pn_piano_t P;

int main(void)
{
    static const int notes[] = {21, 36, 48, 55, 60, 64, 67, 72, 79, 84, 96, 108};
    uint32_t sum = 2166136261u;
    float peak = 0, out[PN_BLOCK];
    int v, b, i, k, worst = 0;
    for (v = 0; v < piano_count(); v++) {
        int32_t s[PN_FIELDS];
        pn_init(&P, 44118.f);
        piano_default(v, s);
        for (k = 1; k < PN_FIELDS; k++)
            piano_setting(&P, k, s[k]);
        for (b = 0; b < 44118 * 4 / PN_BLOCK; b++) {
            if (b == 0)
                pn_pedal(&P, 1);
            if (b % 40 == 0 && b / 40 < 12)
                pn_note_on(&P, notes[b / 40], 20 + (b / 40) * 9);
            if (b == 600)
                pn_pedal(&P, 0);
            pn_render(&P, out);
            if (P.count > worst)
                worst = P.count;
            for (i = 0; i < PN_BLOCK; i++) {
                uint32_t u;
                float a = out[i] < 0 ? -out[i] : out[i];
                if (!(a < 2.f)) {
                    printf("FAIL voicing %d: a sample is not finite or far out of range\n", v);
                    return 1;
                }
                if (a > peak)
                    peak = a;
                memcpy(&u, &out[i], 4);
                sum = (sum ^ u) * 16777619u;
            }
        }
    }
    printf("%08x peak %.4f partials at most %d of %d\n", sum, (double)peak, worst, PN_MODES);
    return 0;
}
