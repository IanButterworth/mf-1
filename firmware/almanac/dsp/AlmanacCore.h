// Almanac: twelve months as twelve sounds, on the MF-1 engine.
//
// A month is a voice, a sky and a place. The voice is a DX7 patch whose first two stacks play the
// music and whose third stack is the weather itself (a whistling wind, dripping rain, a foghorn hum,
// a thunder growl, a glint of snow), tuned to each note and heard under it. The sky also sets the filter, chorus, phaser and drive; the place sets the reverb and
// the echo; the mood shapes how the voice opens and wavers. Everything is written through MF1Core's
// public C API and uses only what a real FM-1 has, so a month sounds the same on the hardware.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The skies, in the order the SKY knob steps through them.
enum { AL_CLEAR, AL_BREEZE, AL_RAIN, AL_FOG, AL_STORM, AL_SNOW, AL_SKY_COUNT };
// The places.
enum { AL_MEADOW, AL_FOREST, AL_MOUNTAINS, AL_COAST, AL_LAKE, AL_VILLAGE, AL_PLACE_COUNT };
// How a key plays: one note, the month's chord, a drone under the left hand, or the chord as a pattern.
enum { AL_SINGLE, AL_CHORD, AL_SPLIT, AL_PATTERN, AL_PLAY_COUNT };
// How the month's chord is spread: close, open (the middle lifted an octave) or wide (a low root added).
enum { AL_CLOSE, AL_OPEN, AL_WIDE, AL_VOICING_COUNT };

// A day in the almanac is a flat list of numbers. Values 0..100 with 50 "as composed" are offsets
// from the month's own voice.
enum {
  AL_MONTH,      // 0 .. 11
  AL_MOOD,       // 0 .. 100: low is wistful (slow, wavering, darker), high is glad (quick, bright)
  AL_WARMTH,     // 0 .. 100, 50 as composed: brightness
  AL_LINGER,     // 0 .. 100, 50 as composed: how long notes hang on
  AL_SHIMMER,    // 0 .. 100: vibrato
  AL_SKY,        // AL_CLEAR .. AL_SNOW
  AL_WEATHER,    // 0 .. 100: how strongly the sky colours the sound (filter, chorus, phaser, drive)
  AL_ELEMENTS,   // 0 .. 100: how loud the weather itself is under the notes
  AL_GUSTS,      // 0 .. 100: how fast the weather moves
  AL_PLACE,      // AL_MEADOW .. AL_VILLAGE
  AL_DISTANCE,   // 0 .. 100: reverb mix
  AL_ECHO,       // 0 .. 100: delay mix
  AL_SPACE,      // 0 .. 100, 50 as the place is: how large the place sounds (reverb decay, echo repeats)
  AL_PLAY,       // AL_SINGLE .. AL_PATTERN
  AL_VOICING,    // AL_CLOSE .. AL_WIDE
  AL_TEMPO,      // 40 .. 200 BPM, for patterns
  AL_STEP,       // 0 .. 4: a pattern's note length, 1/4, 1/8, 1/8T, 1/16, 1/16T
  AL_FIELDS
};

typedef struct {
  char name[11];       // as stored in the voice, 10 characters
  char month[12];      // "January"
  char title[24];      // what the month's voice is called
  char blurb[40];      // a few words on how it sounds
  uint32_t skyTop, skyBottom;  // 0xRRGGBB, a clear day's sky
  uint32_t ground, far;        // the near and the far land
  uint32_t accent;             // titles and highlights
  int season;                  // 0 winter, 1 spring, 2 summer, 3 autumn
  bool mono;
} AlmanacMonth;

int almanac_month_count(void);
bool almanac_month(int index, AlmanacMonth* out);
const char* almanac_sky_name(int sky);
const char* almanac_place_name(int place);
// A word for how a mood value feels: "Gloomy" .. "Elated".
const char* almanac_mood_name(int mood);

// The day a month is composed as.
void almanac_default_day(int month, int32_t out[AL_FIELDS]);
void almanac_clamp(int32_t day[AL_FIELDS]);
void almanac_range(int field, int* lo, int* hi);

// Puts the months in banks A-D of the engine (the same 32 slots in each, the rest empty voices),
// so program changes, SAVE and bank export all work on them. Call after the engine started.
void almanac_install(void);
// Loads the month into the edit buffer, then applies the day.
void almanac_load(const int32_t day[AL_FIELDS]);
// Writes the day to the edit buffer and the effects. Voice changes reach the next note, as on the FM-1.
void almanac_apply(const int32_t day[AL_FIELDS]);
// The 155 bytes of DX7 VCED the day's voice is.
void almanac_voice(const int32_t day[AL_FIELDS], uint8_t out[155]);
// The notes one key plays, at most 8, written to out; returns how many. `left` is whether the key is in
// the left part of the keyboard, which a split gives the drone.
int almanac_key_notes(const int32_t day[AL_FIELDS], int note, bool left, int out[8]);
// The scene: a pixel-art picture of the day, AL_SCENE_W by AL_SCENE_H pixels as 0xRRGGBB, row by row.
// `step` counts animation frames (a few a second); `activity` (0..100) is how much is being played.
#define AL_SCENE_W 80
#define AL_SCENE_H 43
void almanac_scene(const int32_t day[AL_FIELDS], int step, int activity, uint32_t out[AL_SCENE_W * AL_SCENE_H]);
// A DX7 32-voice bank dump (4104 bytes) of the months as composed.
void almanac_export_bank(uint8_t out[4104]);

#ifdef __cplusplus
}
#endif
