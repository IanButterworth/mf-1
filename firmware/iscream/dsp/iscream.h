/* iScream's sundae, as the desktop app defines it: the same fields, ranges and recipes, which the parity test
 * checks against the desktop's. */
#pragma once
#include <stdint.h>

#include "fx.h"

enum { ISC_WHIPPED_CREAM, ISC_HOT_FUDGE, ISC_CARAMEL, ISC_CHOC_CHIPS, ISC_SPRINKLES, ISC_TOPPING_COUNT };

enum {
    ISC_FLAVOR,
    ISC_SCOOPS,     /* 1 .. 3 */
    ISC_SWEET,      /* 0 .. 100, 50 as designed */
    ISC_MELT,       /* 0 .. 100, 50 as designed */
    ISC_FIZZ,       /* 0 .. 100 */
    ISC_CONE_ON,    /* 0 / 1 */
    ISC_CONE_STYLE, /* 0 waffle (low pass), 1 sugar (band pass), 2 cup (high pass) */
    ISC_TOAST,      /* 0 .. 107 */
    ISC_CRUNCH,     /* 0 .. 100 */
    ISC_TOPPING_BASE,
    ISC_FIELDS = ISC_TOPPING_BASE + 4 * ISC_TOPPING_COUNT
};
#define ISC_TOP(topping, knob) (ISC_TOPPING_BASE + (topping) * 4 + (knob))

typedef struct {
    const char *voice_name, *title, *blurb;
    uint32_t color, accent, bits;
    uint8_t pattern, mono, decays, base_spread;
    int8_t det[6], spread[6]; /* OP1 first, as the flavors list them */
    uint8_t mod[6];
    int32_t sundae[ISC_FIELDS];
    uint8_t voice[155]; /* the flavor as designed, VCED (OP6 first) */
} isc_flavor_t;

int isc_flavor_count(void);
const isc_flavor_t *isc_flavor(int index);
void isc_default_sundae(int flavor, int32_t out[ISC_FIELDS]);
void isc_range(int field, int *lo, int *hi);
void isc_clamp(int32_t s[ISC_FIELDS]);
/* The voice a sundae asks for (155 bytes of VCED) and the effects it sets, as the desktop app gives them. */
void isc_voice(const int32_t sundae[ISC_FIELDS], uint8_t out[155]);
void isc_fx(const int32_t sundae[ISC_FIELDS], isc_fx_params_t *out);
