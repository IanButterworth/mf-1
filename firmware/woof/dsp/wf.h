/* Woof's dogs, as the desktop app defines them in Sources/WoofCore: the same fields, ranges, breeds and places,
 * which the parity test checks against the desktop's. The names are wf_ so the test can link both. */
#pragma once
#include <stdint.h>

#include "WoofCore.h"
#include "fx.h"

/* How a breed barks, in the numbers its voice is built from (see the desktop's internal.h). */
typedef struct {
    const char *voice_name, *breed, *word, *blurb;
    uint32_t accent;
    int8_t transpose;
    int16_t formant, rasp;
    uint8_t throat, mouth, mouth_level, breath, gruff, decay;
    int8_t start, rise, fall;
    uint8_t mono, arp_mode;
    int32_t dog[WF_FIELDS];
} wf_breed_t;

/* How repeats and chases run: the arpeggiator's mode (MF-1's numbering), octaves, rate (MF-1's rate index), gate in
 * percent and tempo in BPM. */
typedef struct {
    int on, mode, octaves, rate, gate, tempo;
} wf_pattern_t;

int wf_dog_count(void);
const wf_breed_t *wf_breed(int index);
const char *wf_place_name(int place);
const char *wf_mood_name(int mood);
void wf_default_dog(int dog, int32_t out[WF_FIELDS]);
void wf_range(int field, int *lo, int *hi);
void wf_clamp(int32_t dog[WF_FIELDS]);
void wf_voice(const int32_t dog[WF_FIELDS], uint8_t out[155]);
void wf_fx(const int32_t dog[WF_FIELDS], isc_fx_params_t *out);
void wf_pattern(const int32_t dog[WF_FIELDS], wf_pattern_t *out);
int wf_key_notes(const int32_t dog[WF_FIELDS], int note, int out[8]);
