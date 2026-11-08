// Woof: a dog show on the MF-1 engine, every key a bark.
//
// A dog is a breed's bark, a place and a mood. The bark is a DX7 voice on algorithm 5: the first stack is the
// dog's throat, pitched by the key; the second is its mouth, a fixed-frequency formant that stays put as the
// pitch moves, as a real dog's mouth does; the third is its breath, a short rough burst at the start. A
// pitch envelope gives each bark its rise and fall. Everything is written through MF1Core's public C API
// and uses only what a real FM-1 has, so a dog sounds the same on the hardware.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The places, in the order the PLACE knob steps through them.
enum { WF_PARK, WF_PARLOUR, WF_ROOFTOPS, WF_FARM, WF_RIVER, WF_PLACE_COUNT };
// How a key plays: one bark; barks again and again while held; the pack in chorus, several dogs at once; or the pack
// barking in turn on the arpeggiator.
enum { WF_SINGLE, WF_REPEAT, WF_CHORUS, WF_CHASE, WF_PLAY_COUNT };
// How big the pack is: a pair, three, or a whole litter.
enum { WF_PAIR, WF_TRIO, WF_LITTER, WF_PACK_COUNT };

// A dog's settings are a flat list of numbers. Values 0..100 with 50 "as bred" are offsets from the breed's
// own bark.
enum {
  WF_DOG,       // 0 .. 12
  WF_SIZE,      // 0 .. 100, 50 as bred: small dogs are higher, with a smaller mouth
  WF_GRUFF,     // 0 .. 100, 50 as bred: how rough the throat is
  WF_LENGTH,    // 0 .. 100, 50 as bred: how long a bark lasts
  WF_YELP,      // 0 .. 100, 50 as bred: how far the pitch jumps up in a bark
  WF_MOOD,      // 0 .. 100: low is sad (slow, falling, dark), high is playful (quick, rising, bright)
  WF_HOWL,      // 0 .. 100: from a bark to a long howl
  WF_GROWL,     // 0 .. 100: a rough, shaking growl that carries on while the key is held
  WF_BREATH,    // 0 .. 100: how loud the breath is at the start of a bark
  WF_PLACE,     // WF_PARK .. WF_RIVER
  WF_DISTANCE,  // 0 .. 100: reverb mix
  WF_ECHO,      // 0 .. 100: delay mix, dogs far off answering
  WF_SPACE,     // 0 .. 100, 50 as the place is: how large the place sounds
  WF_PLAY,      // WF_SINGLE .. WF_CHASE
  WF_PACK,      // WF_PAIR .. WF_LITTER
  WF_TEMPO,     // 40 .. 200 BPM, for repeats and chases
  WF_STEP,      // 0 .. 4: how often a dog barks again, 1/4, 1/8, 1/8T, 1/16, 1/16T
  WF_FIELDS
};

typedef struct {
  char name[11];    // as stored in the voice, 10 characters
  char breed[20];   // "Dalmatian"
  char word[8];     // what the bark says: "WOOF"
  char blurb[40];   // a few words on how it sounds
  uint32_t accent;  // 0xRRGGBB, titles and highlights
  bool mono;
} WoofDog;

int woof_dog_count(void);
bool woof_dog(int index, WoofDog* out);
const char* woof_place_name(int place);
// A word for how a mood value feels: "Mopey" .. "Zoomies".
const char* woof_mood_name(int mood);

// The settings a breed is shown with.
void woof_default_dog(int dog, int32_t out[WF_FIELDS]);
void woof_clamp(int32_t dog[WF_FIELDS]);
void woof_range(int field, int* lo, int* hi);

// Puts the dogs in banks A-D of the engine (the same 32 slots in each, the rest empty voices), so program
// changes, SAVE and bank export all work on them. Call after the engine started.
void woof_install(void);
// Loads the dog into the edit buffer, then applies its settings.
void woof_load(const int32_t dog[WF_FIELDS]);
// Writes the settings to the edit buffer and the effects. Voice changes reach the next note, as on the FM-1.
void woof_apply(const int32_t dog[WF_FIELDS]);
// The 155 bytes of DX7 VCED the dog's bark is.
void woof_voice(const int32_t dog[WF_FIELDS], uint8_t out[155]);
// The notes one key plays, at most 8, written to out; returns how many.
int woof_key_notes(const int32_t dog[WF_FIELDS], int note, int out[8]);
// The scene: a pixel-art picture of the dog, WF_SCENE_W by WF_SCENE_H pixels as 0xRRGGBB, row by row.
// `step` counts animation frames (a few a second); `bark` (0..100) is how loudly it is barking now, and
// `arrived` how many frames ago the dog came on, so a new dog trots in.
#define WF_SCENE_W 120
#define WF_SCENE_H 64
void woof_scene(const int32_t dog[WF_FIELDS], int step, int bark, int arrived, uint32_t out[WF_SCENE_W * WF_SCENE_H]);
// A DX7 32-voice bank dump (4104 bytes) of the dogs as bred.
void woof_export_bank(uint8_t out[4104]);

#ifdef __cplusplus
}
#endif
