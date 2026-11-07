/* How a sundae becomes an FM-1 voice and effect settings, ported from the desktop app. */
#include "iscream.h"

#include "isc_libc.h"

#include "flavors.inc"

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* As lround for the halves this file rounds: positive and negative values away from zero. */
static int round_div(int num, int den)
{
    return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
}

int isc_flavor_count(void) { return ISC_FLAVOR_COUNT; }

const isc_flavor_t *isc_flavor(int index)
{
    return &kFlavors[clampi(index, 0, ISC_FLAVOR_COUNT - 1)];
}

void isc_default_sundae(int flavor, int32_t out[ISC_FIELDS])
{
    isc_memcpy(out, isc_flavor(flavor)->sundae, sizeof(int32_t) * ISC_FIELDS);
}

void isc_range(int field, int *lo, int *hi)
{
    int a = 0, b = 100;
    switch (field) {
    case ISC_FLAVOR: b = ISC_FLAVOR_COUNT - 1; break;
    case ISC_SCOOPS: a = 1, b = 3; break;
    case ISC_CONE_ON: b = 1; break;
    case ISC_CONE_STYLE: b = 2; break;
    case ISC_TOAST: b = 107; break;
    default:
        if (field >= ISC_TOPPING_BASE) {
            int topping = (field - ISC_TOPPING_BASE) / 4, knob = (field - ISC_TOPPING_BASE) % 4;
            if (knob == 0)
                b = 1;
            if (knob == 3 && topping == ISC_SPRINKLES)
                b = 2;
        }
    }
    if (lo)
        *lo = a;
    if (hi)
        *hi = b;
}

void isc_clamp(int32_t s[ISC_FIELDS])
{
    int i, lo, hi;
    for (i = 0; i < ISC_FIELDS; i++) {
        isc_range(i, &lo, &hi);
        s[i] = clampi(s[i], lo, hi);
    }
}

static int scoop_spread(int scoops)
{
    static const int steps[3] = {0, 2, 5};
    return steps[clampi(scoops, 1, 3) - 1];
}

/* Everything comes from the flavor as designed plus four macros, each of them ordinary DX7 parameters:
 * Scoops detunes the three stacks, Sweet moves the modulators, Melt the releases (and the decays of the flavors
 * that fall away by themselves), Fizz the vibrato. Caramel adds a slightly flat tuning and a little flutter. */
void isc_voice(const int32_t sundae[ISC_FIELDS], uint8_t v[155])
{
    int32_t s[ISC_FIELDS];
    const isc_flavor_t *f;
    int spread, caramel_amount, flat, sweet_offset, melt, i;
    isc_memcpy(s, sundae, sizeof s);
    isc_clamp(s);
    f = isc_flavor(s[ISC_FLAVOR]);
    isc_memcpy(v, f->voice, 155);

    spread = f->base_spread + scoop_spread(s[ISC_SCOOPS]);
    caramel_amount = s[ISC_TOP(ISC_CARAMEL, 0)] ? s[ISC_TOP(ISC_CARAMEL, 1)] : 0;
    flat = round_div(caramel_amount * 3, 100);
    /* (sweet - 50) * 0.6 below 50 and * 0.24 above. */
    sweet_offset = s[ISC_SWEET] < 50 ? round_div((s[ISC_SWEET] - 50) * 6, 10) : round_div((s[ISC_SWEET] - 50) * 24, 100);
    melt = round_div((s[ISC_MELT] - 50) * 5, 10);

    for (i = 0; i < 6; i++) {
        const uint8_t *base = f->voice + (5 - i) * 21;
        uint8_t *p = v + (5 - i) * 21;
        p[20] = (uint8_t)clampi(f->det[i] + f->spread[i] * spread - flat, 0, 14);
        if (f->mod[i])
            p[16] = (uint8_t)clampi(base[16] + sweet_offset, 0, 99);
        p[3] = (uint8_t)clampi(base[3] - melt, 1, 99);
        if (f->decays) {
            p[1] = (uint8_t)clampi(base[1] - melt, 1, 99);
            p[2] = (uint8_t)clampi(base[2] - melt, 1, 99);
        }
    }
    /* Fizz * 0.35 + caramel * 0.12. */
    v[139] = (uint8_t)clampi(round_div(s[ISC_FIZZ] * 35 + caramel_amount * 12, 100), 0, 99);
}

/* The cone first, then the effects as a signal would meet them: drive, swirl, chorus, echo, room. */
static const uint8_t kChain[ISC_FX_COUNT] = {ISC_FX_FILTER, ISC_FX_DIST, ISC_FX_PHASER,
                                             ISC_FX_CHORUS, ISC_FX_DELAY, ISC_FX_REVERB};

void isc_fx(const int32_t sundae[ISC_FIELDS], isc_fx_params_t *p)
{
    int32_t s[ISC_FIELDS];
    isc_memcpy(s, sundae, sizeof s);
    isc_clamp(s);
    isc_fx_params_default(p);
    isc_memcpy(p->order, kChain, sizeof kChain);
#define TOP(t, k) s[ISC_TOP(t, k)]
    isc_fx_params_set(p, 1, s[ISC_CONE_STYLE]);
    isc_fx_params_set(p, 2, s[ISC_TOAST]);
    /* Q at the panel's resolution, 0..100, as MF-1's engine takes it from iScream. */
    p->cc[3] = (uint8_t)s[ISC_CRUNCH];
    isc_fx_params_set(p, 0, s[ISC_CONE_ON]);
    /* Whipped cream: chorus. */
    isc_fx_params_set(p, 17, TOP(ISC_WHIPPED_CREAM, 2));
    isc_fx_params_set(p, 18, TOP(ISC_WHIPPED_CREAM, 3));
    isc_fx_params_set(p, 19, TOP(ISC_WHIPPED_CREAM, 1));
    isc_fx_params_set(p, 16, TOP(ISC_WHIPPED_CREAM, 0));
    /* Hot fudge: distortion. */
    isc_fx_params_set(p, 13, TOP(ISC_HOT_FUDGE, 1));
    isc_fx_params_set(p, 14, TOP(ISC_HOT_FUDGE, 2));
    isc_fx_params_set(p, 15, TOP(ISC_HOT_FUDGE, 3));
    isc_fx_params_set(p, 12, TOP(ISC_HOT_FUDGE, 0));
    /* Caramel: phaser. */
    isc_fx_params_set(p, 21, TOP(ISC_CARAMEL, 2));
    isc_fx_params_set(p, 22, TOP(ISC_CARAMEL, 3));
    isc_fx_params_set(p, 23, TOP(ISC_CARAMEL, 1));
    isc_fx_params_set(p, 20, TOP(ISC_CARAMEL, 0));
    /* Chocolate chips: delay. */
    isc_fx_params_set(p, 9, TOP(ISC_CHOC_CHIPS, 2));
    isc_fx_params_set(p, 10, TOP(ISC_CHOC_CHIPS, 3));
    isc_fx_params_set(p, 11, TOP(ISC_CHOC_CHIPS, 1));
    isc_fx_params_set(p, 8, TOP(ISC_CHOC_CHIPS, 0));
    /* Rainbow sprinkles: reverb. */
    isc_fx_params_set(p, 5, TOP(ISC_SPRINKLES, 3));
    isc_fx_params_set(p, 6, TOP(ISC_SPRINKLES, 2));
    isc_fx_params_set(p, 7, TOP(ISC_SPRINKLES, 1));
    isc_fx_params_set(p, 4, TOP(ISC_SPRINKLES, 0));
#undef TOP
}
