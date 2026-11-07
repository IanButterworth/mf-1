/* The FM-1 effect section, ported from MF-1 to single-precision C at 44.1 kHz: six effects in a reorderable
 * mono chain. Parameters use the V15 FX CC numbering (0..23) and ranges. */
#pragma once
#include <stdint.h>

enum { ISC_FX_FILTER, ISC_FX_REVERB, ISC_FX_DELAY, ISC_FX_DIST, ISC_FX_CHORUS, ISC_FX_PHASER, ISC_FX_COUNT };

typedef struct {
    uint8_t order[ISC_FX_COUNT];
    uint8_t cc[24]; /* as the V15 FX CCs; Q (cc 3) is kept as 0..100, as the panel shows it */
} isc_fx_params_t;

void isc_fx_params_default(isc_fx_params_t *p);
/* Sets an FX CC as MF-1 does: switches take any value of 1 or more as on, Q 0..10 is stored as 0..100. */
void isc_fx_params_set(isc_fx_params_t *p, int cc, int value);
int isc_fx_params_get(const isc_fx_params_t *p, int cc);

void isc_fx_init(void);
void isc_fx_set_params(const isc_fx_params_t *p);
/* In place, mono, any length. */
void isc_fx_process(float *x, int frames);
