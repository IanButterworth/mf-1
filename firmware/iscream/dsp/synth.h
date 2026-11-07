/* The voices and the signal path, ported from MF-1: 12 voices allocated round robin from a cursor as on the
 * FM-1, mono with legato, sustain, the global LFO, then the effects. It has one owner, the audio interrupt, which
 * takes the screen's changes from a queue first. */
#pragma once
#include <stdint.h>

#include "fm.h"
#include "fx.h"

#define ISC_POLY 12

void isc_synth_init(void);
/* A new voice (155 VCED bytes): sounding notes keep their old data, as on the FM-1 with V15 behaviour. */
void isc_synth_set_voice(const uint8_t voice[155], int mono);
void isc_synth_set_fx(const isc_fx_params_t *p);
void isc_synth_note_on(int note, int velocity);
void isc_synth_note_off(int note);
void isc_synth_sustain(int on);
void isc_synth_pitch_bend(int value14);
void isc_synth_mod_wheel(int value);
void isc_synth_aftertouch(int value);
void isc_synth_all_notes_off(void);
/* One 64-sample block of mono after the effects, full scale 1.0. */
void isc_synth_block(float out[FM_N]);
/* A bit per sounding note, for the key LEDs. */
int isc_synth_sounding(int note);
int isc_synth_active_voices(void);
