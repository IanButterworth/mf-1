/* The FM-1 effects, ported from MF-1. The designs are MF-1's generic fits to the published CC ranges and the
 * community's measurements, not M-VAVE's DSP, which is unpublished.
 * Differences from MF-1: single precision throughout, a fixed 44.1 kHz rate, and a delay line of 0.8 s, the
 * longest delay the rate knob reaches, so every buffer fits in the FM-1's RAM. */
#include "fx.h"

#include "isc_libc.h"

#include "isc_math.h"

#define SR 44100.0f
#define CTL_BLOCK 16
#define LINES 8
#define CHORUS_LEN 2209          /* 0.05 s */
#define DELAY_LEN 35288          /* 0.8 s */
#define PREDELAY_LEN 2213        /* 0.05 s */

enum {
    CC_FILTER_ON, CC_FILTER_TYPE, CC_CUTOFF, CC_Q,
    CC_REVERB_ON, CC_REVERB_TYPE, CC_REVERB_DECAY, CC_REVERB_MIX,
    CC_DELAY_ON, CC_DELAY_DECAY, CC_DELAY_RATE, CC_DELAY_MIX,
    CC_DIST_ON, CC_DIST_GAIN, CC_DIST_TONE, CC_DIST_LEVEL,
    CC_CHORUS_ON, CC_CHORUS_FREQ, CC_CHORUS_DEPTH, CC_CHORUS_MIX,
    CC_PHASER_ON, CC_PHASER_FREQ, CC_PHASER_DEPTH, CC_PHASER_MIX
};

typedef struct {
    float lines[LINES];
    float diffusers[4];
    float predelay_ms;
    float damp_hz;  /* cutoff of the one pole lowpass inside the loop */
    float rt_scale; /* RT40 relative to Hall at the same decay */
} reverb_shape_t;

static const reverb_shape_t kShapes[3] = {
    /* Room: short and dense. */
    {{11.3f, 13.7f, 15.1f, 17.9f, 19.3f, 21.7f, 23.9f, 26.1f}, {1.7f, 2.9f, 4.1f, 5.3f}, 4, 20000, 0.74f},
    /* Hall: long lines, more predelay. */
    {{37.1f, 41.3f, 47.9f, 53.1f, 59.3f, 63.7f, 71.9f, 79.3f}, {3.1f, 4.7f, 7.3f, 9.7f}, 18, 17000, 1.f},
    /* Plate: no predelay, bright, very diffuse. */
    {{17.3f, 19.9f, 23.3f, 27.1f, 29.9f, 33.7f, 37.3f, 41.9f}, {1.1f, 1.9f, 2.7f, 3.9f}, 0, 20000, 0.86f},
};
/* The longest line of each position across the three shapes, in samples, with room to spare. */
#define FDN_MAX 3520
#define DIFF_MAX 440

/* Hall RT40 in seconds at decay 0, 5, 25, 50, 75 and 100, as measured on the FM-1. */
static const float kDecayPoints[6] = {0, 5, 25, 50, 75, 100};
static const float kHallRt40[6] = {0.12f, 0.40f, 1.5f, 2.3f, 3.7f, 5.0f};

static float hall_rt40(int decay)
{
    int i;
    for (i = 1; i < 6; i++) {
        if ((float)decay <= kDecayPoints[i]) {
            float t = ((float)decay - kDecayPoints[i - 1]) / (kDecayPoints[i] - kDecayPoints[i - 1]);
            return kHallRt40[i - 1] + t * (kHallRt40[i] - kHallRt40[i - 1]);
        }
    }
    return kHallRt40[5];
}

/* The manual gives 0.8 s down to 0.1 s but not the curve, so take the plain linear map. */
static float delay_samples(int rate) { return (0.8f - 0.7f * (float)rate / 100.0f) * SR; }

static float one_pole_coef(float hz) { return 1.0f - isc_expf(-2.0f * ISC_PI * hz / SR); }

static float fminf_(float a, float b) { return a < b ? a : b; }
static float fmaxf_(float a, float b) { return a > b ? a : b; }
static float fabsf_(float x) { return x < 0 ? -x : x; }

/* Pade approximant of tanh: close enough for a soft clipper and much cheaper. */
static inline float fast_tanh(float x)
{
    float x2, num, den;
    x = fmaxf_(-4.97f, fminf_(4.97f, x));
    x2 = x * x;
    num = x * (135135.f + x2 * (17325.f + x2 * (378.f + x2)));
    den = 135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f));
    return num / den;
}

/* ------------------------------------------------------------------ parameters */

static const uint8_t kDefaults[24] = {0, 0, 107, 0, 0, 0, 50, 30, 0, 40, 40, 30,
                                      0, 30, 50, 50, 0, 30, 50, 50, 0, 30, 50, 50};

static int cc_max(int cc)
{
    switch (cc) {
    case 0: case 4: case 8: case 12: case 16: case 20: return 1;
    case 1: case 5: return 2;
    case 2: return 107;
    case 3: return 10;
    }
    return cc < 24 ? 100 : 0;
}

void isc_fx_params_default(isc_fx_params_t *p)
{
    int i;
    for (i = 0; i < ISC_FX_COUNT; i++)
        p->order[i] = (uint8_t)i;
    isc_memcpy(p->cc, kDefaults, sizeof p->cc);
}

void isc_fx_params_set(isc_fx_params_t *p, int cc, int value)
{
    int m;
    if (cc < 0 || cc >= 24)
        return;
    m = cc_max(cc);
    if (m == 1)
        value = value >= 1 ? 1 : 0;
    value = value < 0 ? 0 : value > m ? m : value;
    p->cc[cc] = (uint8_t)(cc == CC_Q ? value * 10 : value);
}

int isc_fx_params_get(const isc_fx_params_t *p, int cc)
{
    if (cc < 0 || cc >= 24)
        return 0;
    return cc == CC_Q ? (p->cc[CC_Q] + 5) / 10 : p->cc[cc];
}

static int slot_on(const isc_fx_params_t *p, int slot)
{
    static const uint8_t on_cc[ISC_FX_COUNT] = {CC_FILTER_ON, CC_REVERB_ON, CC_DELAY_ON,
                                                CC_DIST_ON,   CC_CHORUS_ON, CC_PHASER_ON};
    return p->cc[on_cc[slot]] != 0;
}

/* ------------------------------------------------------------------ state */

typedef struct {
    float value, target;
} smoothed_t;

static float smooth_next(smoothed_t *s, float coef) { return s->value += (s->target - s->value) * coef; }

static isc_fx_params_t p_;
static float smooth_coef;

static float svf_ic1, svf_ic2;
static smoothed_t cutoff_, q_;
static float filter_c = -1, filter_q = -1, filter_k, filter_a1, filter_a2, filter_a3;

static float tone_state;

static ISC_POOL float chorus_buf[CHORUS_LEN];
static int chorus_pos;
static float chorus_phase;
static float chorus_delay[2];
static int chorus_ready, phaser_ready;

static float ap_state[6], phaser_fb, phaser_phase, phaser_coef;

static ISC_POOL float delay_buf[DELAY_LEN];
static int delay_pos;
static smoothed_t delay_time;
static float delay_damp, delay_damp_coef;

static ISC_POOL float fdn[LINES][FDN_MAX];
static int fdn_len[LINES], fdn_pos[LINES];
static float fdn_damp[LINES], fdn_gain[LINES];
static ISC_POOL float diff[4][DIFF_MAX];
static int diff_len[4], diff_pos[4];
static ISC_POOL float predelay[PREDELAY_LEN];
static int predelay_pos, predelay_len;
static float reverb_damp_coef = 0.7f;
static int reverb_type_built = -1;

static void update_reverb_gains(void)
{
    const reverb_shape_t *s = &kShapes[reverb_type_built < 0 ? 0 : reverb_type_built];
    float rt40 = hall_rt40(p_.cc[CC_REVERB_DECAY]) * s->rt_scale;
    /* The loop lowpass takes some level on every pass. Give it back at 1 kHz,
     * so the midrange of the tail decays at the measured rate. */
    float a = reverb_damp_coef;
    float w = 2 * ISC_PI * 1000.0f / SR;
    float damp_gain = a / isc_sqrtf(1 - 2 * (1 - a) * isc_cosf(w) + (1 - a) * (1 - a));
    int i;
    for (i = 0; i < LINES; i++) {
        float seconds = (float)fdn_len[i] / SR;
        float g = isc_powf(10.f, -2.f * seconds / rt40) / damp_gain;
        fdn_gain[i] = fminf_(g, 0.999f);
    }
}

static void build_reverb(int type)
{
    const reverb_shape_t *s = &kShapes[type];
    float scale = SR / 1000.0f;
    int i;
    for (i = 0; i < LINES; i++) {
        int n = (int)(s->lines[i] * scale);
        fdn_len[i] = n < 8 ? 8 : n;
        fdn_pos[i] = 0;
        fdn_damp[i] = 0;
    }
    for (i = 0; i < 4; i++) {
        int n = (int)(s->diffusers[i] * scale);
        diff_len[i] = n < 4 ? 4 : n;
        diff_pos[i] = 0;
    }
    isc_memset(fdn, 0, sizeof fdn);
    isc_memset(diff, 0, sizeof diff);
    predelay_len = (int)(s->predelay_ms * scale);
    if (predelay_len < 1)
        predelay_len = 1;
    predelay_pos = 0;
    isc_memset(predelay, 0, sizeof predelay);
    reverb_damp_coef = one_pole_coef(s->damp_hz);
    reverb_type_built = type;
    update_reverb_gains();
}

void isc_fx_init(void)
{
    isc_fx_params_default(&p_);
    smooth_coef = 1.0f - isc_expf(-1.0f / (0.02f * SR));
    isc_memset(chorus_buf, 0, sizeof chorus_buf);
    isc_memset(delay_buf, 0, sizeof delay_buf);
    chorus_pos = delay_pos = 0;
    svf_ic1 = svf_ic2 = tone_state = 0;
    chorus_phase = phaser_phase = phaser_fb = phaser_coef = delay_damp = 0;
    chorus_delay[0] = chorus_delay[1] = 0;
    isc_memset(ap_state, 0, sizeof ap_state);
    reverb_type_built = -1;
    build_reverb(p_.cc[CC_REVERB_TYPE]);
    cutoff_.value = cutoff_.target = p_.cc[CC_CUTOFF];
    q_.value = q_.target = p_.cc[CC_Q];
    delay_time.value = delay_time.target = delay_samples(p_.cc[CC_DELAY_RATE]);
    /* Each echo loses treble. */
    delay_damp_coef = one_pole_coef(3300);
    filter_c = -1;
    chorus_ready = phaser_ready = 0;
}

void isc_fx_set_params(const isc_fx_params_t *p)
{
    int decay_changed = p->cc[CC_REVERB_DECAY] != p_.cc[CC_REVERB_DECAY];
    p_ = *p;
    if (p_.cc[CC_REVERB_TYPE] != reverb_type_built)
        build_reverb(p_.cc[CC_REVERB_TYPE]);
    else if (decay_changed)
        update_reverb_gains();
    cutoff_.target = p_.cc[CC_CUTOFF];
    q_.target = p_.cc[CC_Q];
    delay_time.target = delay_samples(p_.cc[CC_DELAY_RATE]);
}

/* ------------------------------------------------------------------ effects */

static void run_filter(float *x, int n)
{
    const float coef = smooth_coef;
    int start, i, type = p_.cc[CC_FILTER_TYPE];
    for (start = 0; start < n; start += CTL_BLOCK) {
        int end = start + CTL_BLOCK < n ? start + CTL_BLOCK : n;
        float k, a1, a2, a3;
        for (i = start; i < end; i++) {
            smooth_next(&cutoff_, coef);
            smooth_next(&q_, coef);
        }
        /* Snap when close, so the coefficients stop being recomputed. */
        if (fabsf_(cutoff_.target - cutoff_.value) < 1e-3f)
            cutoff_.value = cutoff_.target;
        if (fabsf_(q_.target - q_.value) < 1e-3f)
            q_.value = q_.target;
        if (cutoff_.value != filter_c || q_.value != filter_q) {
            float hz, g;
            filter_c = cutoff_.value;
            filter_q = q_.value;
            hz = 100.f * isc_powf(200.f, filter_c / 107.f);
            hz = fminf_(hz, SR * 0.45f);
            g = isc_tanf(ISC_PI * hz / SR);
            /* Damping: Q 0 is a gentle Butterworth-like slope, Q 100 rings hard. */
            filter_k = 1.414f - 1.35f * (filter_q / 100.f);
            filter_a1 = 1.f / (1.f + g * (g + filter_k));
            filter_a2 = g * filter_a1;
            filter_a3 = g * filter_a2;
        }
        k = filter_k, a1 = filter_a1, a2 = filter_a2, a3 = filter_a3;
        for (i = start; i < end; i++) {
            float v0 = x[i];
            float v3 = v0 - svf_ic2;
            float v1 = a1 * svf_ic1 + a2 * v3;
            float v2 = svf_ic2 + a2 * svf_ic1 + a3 * v3;
            svf_ic1 = 2 * v1 - svf_ic1;
            svf_ic2 = 2 * v2 - svf_ic2;
            x[i] = type == 0 ? v2 : type == 1 ? v1 * k : v0 - k * v1 - v2;
        }
    }
}

static void run_distortion(float *x, int n)
{
    float g = p_.cc[CC_DIST_GAIN] / 100.f;
    float g2 = g * g;
    float drive = 1.f + g2 * 60.f;
    float tone_hz = 800.f * isc_powf(2.f, p_.cc[CC_DIST_TONE] / 100.f * 4.5f);
    float tone_a = 1.f - isc_expf(-2.f * ISC_PI * tone_hz / SR);
    /* Quiet signals pass at about unity at any gain; high gain gets up to 9.5 dB of makeup. */
    float out = (1.f + 2.f * g2) / drive;
    /* A plain linear trim: level 20 is about -14 dB, as measured. */
    float level = p_.cc[CC_DIST_LEVEL] / 100.f;
    float s = tone_state;
    int i;
    for (i = 0; i < n; i++) {
        float y = fast_tanh(x[i] * drive) * out;
        s += tone_a * (y - s);
        x[i] = s * level;
    }
    tone_state = s;
}

static float chorus_tap(float base, float depth, int t)
{
    return base + depth * 0.5f * (1.f + isc_sinf(2 * ISC_PI * (chorus_phase + t * 0.25f)));
}

static void run_chorus(float *x, int n)
{
    /* 0.1 to 1 Hz, per the FM-1+VA manual. */
    float rate = 0.1f * isc_powf(10.0f, p_.cc[CC_CHORUS_FREQ] / 100.0f);
    float inc = rate / SR;
    float depth = (0.0005f + p_.cc[CC_CHORUS_DEPTH] / 100.0f * 0.006f) * SR;
    float base = 0.008f * SR;
    float mix = p_.cc[CC_CHORUS_MIX] / 100.f;
    int start, i, t;
    if (!chorus_ready) {
        chorus_delay[0] = chorus_tap(base, depth, 0);
        chorus_delay[1] = chorus_tap(base, depth, 1);
        chorus_ready = 1;
    }
    for (start = 0; start < n; start += CTL_BLOCK) {
        int end = start + CTL_BLOCK < n ? start + CTL_BLOCK : n;
        float d[2], step[2];
        chorus_phase += inc * (float)(end - start);
        if (chorus_phase >= 1)
            chorus_phase -= 1;
        /* The LFO is slow, so the delay can move in straight lines between control points. */
        for (t = 0; t < 2; t++) {
            d[t] = chorus_delay[t];
            chorus_delay[t] = chorus_tap(base, depth, t);
            step[t] = (chorus_delay[t] - d[t]) / (float)(end - start);
        }
        for (i = start; i < end; i++) {
            float wet = 0;
            chorus_buf[chorus_pos] = x[i];
            for (t = 0; t < 2; t++) {
                float rp, frac;
                int i0, i1;
                d[t] += step[t];
                rp = (float)chorus_pos - d[t];
                while (rp < 0)
                    rp += CHORUS_LEN;
                i0 = (int)rp;
                frac = rp - (float)i0;
                i1 = (i0 + 1) % CHORUS_LEN;
                wet += 0.5f * (chorus_buf[i0] * (1 - frac) + chorus_buf[i1] * frac);
            }
            x[i] = x[i] * (1 - mix * 0.5f) + wet * mix;
            chorus_pos = (chorus_pos + 1) % CHORUS_LEN;
        }
    }
}

static float allpass_coef(float depth)
{
    float lfo = 0.5f + 0.5f * isc_sinf(2 * ISC_PI * phaser_phase);
    float hz = 200.f * isc_powf(2.f, lfo * depth * 5.f);
    float t = isc_tanf(ISC_PI * hz / SR);
    return (t - 1.f) / (t + 1.f);
}

static void run_phaser(float *x, int n)
{
    /* 0.5 to 6 Hz, per the FM-1+VA manual. */
    float rate = 0.5f * isc_powf(12.0f, p_.cc[CC_PHASER_FREQ] / 100.0f);
    float inc = rate / SR;
    float depth = p_.cc[CC_PHASER_DEPTH] / 100.f;
    float mix = p_.cc[CC_PHASER_MIX] / 100.f;
    int start, i, s;
    if (!phaser_ready) {
        phaser_coef = allpass_coef(depth);
        phaser_ready = 1;
    }
    for (start = 0; start < n; start += CTL_BLOCK) {
        int end = start + CTL_BLOCK < n ? start + CTL_BLOCK : n;
        float a, step;
        phaser_phase += inc * (float)(end - start);
        if (phaser_phase >= 1)
            phaser_phase -= 1;
        a = phaser_coef;
        phaser_coef = allpass_coef(depth);
        step = (phaser_coef - a) / (float)(end - start);
        for (i = start; i < end; i++) {
            float v;
            a += step;
            v = x[i] + phaser_fb * 0.5f;
            for (s = 0; s < 6; s++) {
                float y = a * v + ap_state[s];
                ap_state[s] = v - a * y;
                v = y;
            }
            phaser_fb = v;
            x[i] = x[i] * (1 - mix * 0.5f) + v * mix * 0.7f;
        }
    }
}

static void run_delay(float *x, int n)
{
    float fb = (p_.cc[CC_DELAY_DECAY] / 100.f) * 0.92f;
    float mix = p_.cc[CC_DELAY_MIX] / 100.f;
    int i;
    for (i = 0; i < n; i++) {
        float d = smooth_next(&delay_time, smooth_coef * 0.2f);
        float rp = (float)delay_pos - d;
        float frac, tap;
        int i0, i1;
        while (rp < 0)
            rp += DELAY_LEN;
        i0 = (int)rp;
        frac = rp - (float)i0;
        i1 = (i0 + 1) % DELAY_LEN;
        tap = delay_buf[i0] * (1 - frac) + delay_buf[i1] * frac;
        delay_damp += delay_damp_coef * (tap - delay_damp);
        delay_buf[delay_pos] = x[i] + delay_damp * fb;
        x[i] += tap * mix;
        delay_pos = (delay_pos + 1) % DELAY_LEN;
    }
}

static void run_reverb(float *x, int n)
{
    float mix = p_.cc[CC_REVERB_MIX] / 100.f;
    float damp = reverb_damp_coef;
    int i, d, k;
    for (i = 0; i < n; i++) {
        float v, sum = 0, h, wet = 0, outs[LINES];
        int rp;
        predelay[predelay_pos] = x[i];
        rp = predelay_pos - predelay_len;
        if (rp < 0)
            rp += PREDELAY_LEN;
        v = predelay[rp];
        predelay_pos = (predelay_pos + 1) % PREDELAY_LEN;
        for (d = 0; d < 4; d++) {
            float delayed = diff[d][diff_pos[d]];
            float w = v + 0.6f * delayed;
            diff[d][diff_pos[d]] = w;
            v = delayed - 0.6f * w;
            if (++diff_pos[d] >= diff_len[d])
                diff_pos[d] = 0;
        }
        for (k = 0; k < LINES; k++) {
            float o = fdn[k][fdn_pos[k]];
            fdn_damp[k] += damp * (o - fdn_damp[k]);
            outs[k] = fdn_damp[k] * fdn_gain[k];
            sum += outs[k];
        }
        /* Householder feedback matrix keeps the network lossless before the gains. */
        h = sum * (2.f / LINES);
        for (k = 0; k < LINES; k++) {
            fdn[k][fdn_pos[k]] = outs[k] - h + v;
            if (++fdn_pos[k] >= fdn_len[k])
                fdn_pos[k] = 0;
            wet += (k & 1) ? -outs[k] : outs[k];
        }
        x[i] = x[i] * (1 - mix * 0.5f) + wet * 0.25f * mix;
    }
}

void isc_fx_process(float *x, int frames)
{
    int i;
    for (i = 0; i < ISC_FX_COUNT; i++) {
        int slot = p_.order[i];
        if (!slot_on(&p_, slot))
            continue;
        switch (slot) {
        case ISC_FX_FILTER: run_filter(x, frames); break;
        case ISC_FX_REVERB: run_reverb(x, frames); break;
        case ISC_FX_DELAY: run_delay(x, frames); break;
        case ISC_FX_DIST: run_distortion(x, frames); break;
        case ISC_FX_CHORUS: run_chorus(x, frames); break;
        case ISC_FX_PHASER: run_phaser(x, frames); break;
        }
    }
    if (!p_.cc[CC_FILTER_ON]) {
        cutoff_.value = cutoff_.target;
        q_.value = q_.target;
    }
    if (!p_.cc[CC_DELAY_ON])
        delay_time.value = delay_time.target;
}
