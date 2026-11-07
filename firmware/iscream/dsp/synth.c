/* Voice allocation and the mix, ported from MF-1. Left out: the arpeggiator, sequencer and
 * song player, the ENV overlay, glide, operator mutes and MF-1's model of the FM-1's DAC and output filters (on
 * the device those are the hardware). */
#include "synth.h"

#include "isc_libc.h"

typedef struct {
    fm_voice_t dx;
    int note;
    uint32_t serial;
    uint8_t key_down, sustained, active;
} slot_t;

typedef struct {
    int note;
    uint32_t serial;
} mono_key_t;

static uint8_t voice_[FM_VOICE_SIZE];
static int mono_;
static fm_ctrl_t ctrl_;
static fm_lfo_t lfo_;
static slot_t slots_[ISC_POLY];
static int cursor_;
static int sustain_;
static mono_key_t mono_stack_[32];
static int mono_count_;
static uint32_t next_serial_;
static int active_;
static int32_t acc_[FM_N], tmp_[FM_N];

static void keyup_slot(slot_t *v) { fm_voice_keyup(&v->dx); }

void isc_synth_all_notes_off(void)
{
    int i;
    for (i = 0; i < ISC_POLY; i++) {
        slot_t *v = &slots_[i];
        if (v->active && (v->key_down || v->sustained))
            keyup_slot(v);
        v->key_down = 0;
        v->sustained = 0;
    }
    mono_count_ = 0;
}

void isc_synth_init(void)
{
    isc_memset(slots_, 0, sizeof slots_);
    isc_memset(&lfo_, 0, sizeof lfo_);
    cursor_ = sustain_ = mono_count_ = 0;
    next_serial_ = 0;
    fm_ctrl_init(&ctrl_);
    isc_fx_init();
}

void isc_synth_set_voice(const uint8_t voice[155], int mono)
{
    if (mono != mono_)
        isc_synth_all_notes_off();
    mono_ = mono;
    isc_memcpy(voice_, voice, 155);
    voice_[FM_OP_ENABLE] = 0x3f;
    fm_lfo_reset(&lfo_, voice_ + 137);
}

void isc_synth_set_fx(const isc_fx_params_t *p) { isc_fx_set_params(p); }

static void play_note(int note, int velocity)
{
    int chosen, i;
    slot_t *v;
    note = note < 0 ? 0 : note > 127 ? 127 : note;
    fm_lfo_keydown(&lfo_);
    if (mono_) {
        for (i = 0; i < mono_count_; i++)
            if (mono_stack_[i].note == note) {
                isc_memmove(&mono_stack_[i], &mono_stack_[i + 1], sizeof(mono_key_t) * (unsigned)(mono_count_ - i - 1));
                mono_count_--;
                break;
            }
        /* The sounding note must stay on the stack, so a full stack forgets its oldest key. */
        if (mono_count_ == 32)
            isc_memmove(&mono_stack_[0], &mono_stack_[1], sizeof(mono_key_t) * (unsigned)--mono_count_);
        v = &slots_[0];
        if (v->active && v->key_down) {
            /* Legato: new pitch, envelopes keep running. */
            fm_voice_transfer(&v->dx, note);
            fm_voice_update(&v->dx, voice_);
        } else {
            fm_voice_init(&v->dx, voice_, note, velocity, v->active);
            v->active = 1;
        }
        v->note = note;
        v->key_down = 1;
        v->sustained = 0;
        v->serial = ++next_serial_;
        mono_stack_[mono_count_].note = note;
        mono_stack_[mono_count_].serial = v->serial;
        mono_count_++;
        return;
    }
    /* Round robin from a cursor: the first voice whose key is up, else the voice at the cursor is stolen. */
    chosen = cursor_;
    for (i = 0; i < ISC_POLY; i++) {
        int ix = (cursor_ + i) % ISC_POLY;
        if (!slots_[ix].key_down) {
            chosen = ix;
            break;
        }
    }
    cursor_ = (chosen + 1) % ISC_POLY;
    v = &slots_[chosen];
    fm_voice_init(&v->dx, voice_, note, velocity, v->active);
    v->note = note;
    v->key_down = 1;
    v->sustained = 0;
    v->active = 1;
    v->serial = ++next_serial_;
}

/* Removes one entry from the mono key stack. If it was the sounding note, the voice goes back to the latest
 * key still held, or is released. */
static void mono_drop(int index)
{
    mono_key_t gone = mono_stack_[index];
    slot_t *v = &slots_[0];
    isc_memmove(&mono_stack_[index], &mono_stack_[index + 1], sizeof(mono_key_t) * (unsigned)(mono_count_ - index - 1));
    mono_count_--;
    if (!v->active || !v->key_down || v->serial != gone.serial)
        return;
    if (mono_count_ > 0) {
        const mono_key_t *back = &mono_stack_[mono_count_ - 1];
        fm_voice_transfer(&v->dx, back->note);
        fm_voice_update(&v->dx, voice_);
        v->note = back->note;
        v->serial = back->serial;
        return;
    }
    v->key_down = 0;
    if (sustain_)
        v->sustained = 1;
    else
        keyup_slot(v);
}

void isc_synth_note_on(int note, int velocity)
{
    if (velocity == 0)
        isc_synth_note_off(note);
    else
        play_note(note, velocity);
}

void isc_synth_note_off(int note)
{
    int i;
    if (mono_) {
        for (i = 0; i < mono_count_; i++)
            if (mono_stack_[i].note == note) {
                mono_drop(i);
                break;
            }
        return;
    }
    for (i = 0; i < ISC_POLY; i++) {
        slot_t *v = &slots_[i];
        if (v->active && v->key_down && v->note == note) {
            v->key_down = 0;
            if (sustain_)
                v->sustained = 1;
            else
                keyup_slot(v);
        }
    }
}

void isc_synth_sustain(int on)
{
    int i;
    if (sustain_ && !on)
        for (i = 0; i < ISC_POLY; i++)
            if (slots_[i].sustained) {
                slots_[i].sustained = 0;
                keyup_slot(&slots_[i]);
            }
    sustain_ = on;
}

void isc_synth_pitch_bend(int value14) { ctrl_.pitch_bend = value14; }
void isc_synth_mod_wheel(int value) { ctrl_.mod_wheel = value; }
void isc_synth_aftertouch(int value) { ctrl_.aftertouch = value; }

void isc_synth_block(float out[FM_N])
{
    int32_t lfo_val = fm_lfo_sample(&lfo_);
    int32_t lfo_delay = fm_lfo_delay(&lfo_);
    int i, k, count = 0;
    isc_memset(acc_, 0, sizeof acc_);
    for (k = 0; k < ISC_POLY; k++) {
        slot_t *v = &slots_[k];
        if (!v->active)
            continue;
        count++;
        isc_memset(tmp_, 0, sizeof tmp_);
        fm_voice_compute(&v->dx, tmp_, lfo_val, lfo_delay, &ctrl_);
        /* msfa's output stage: each voice >> 4 and saturated, so 2^24 is full scale. */
        for (i = 0; i < FM_N; i++) {
            int32_t s = tmp_[i] >> 4;
            acc_[i] += s < -(1 << 24) ? -(1 << 24) : s > (1 << 24) - 1 ? (1 << 24) - 1 : s;
        }
        if (!v->key_down && !v->sustained && fm_voice_finished(&v->dx))
            v->active = 0;
    }
    active_ = count;
    for (i = 0; i < FM_N; i++)
        out[i] = (float)acc_[i] * (1.f / (float)(1 << 24));
    isc_fx_process(out, FM_N);
}

int isc_synth_sounding(int note)
{
    int i;
    for (i = 0; i < ISC_POLY; i++)
        if (slots_[i].active && slots_[i].note == note && (slots_[i].key_down || slots_[i].sustained))
            return 1;
    return 0;
}

int isc_synth_active_voices(void) { return active_; }
