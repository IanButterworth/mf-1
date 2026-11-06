// iScream: a small, bright "operating system" for the FM-1 sound engine.
//
// A sundae is one scoop (a DX7 voice, called a flavor), a cone (the FM-1 filter) and up to five
// toppings (the other FM-1 effects). Everything here is written through MF1Core's public C API
// and uses only what a real FM-1 has: DX7 voice parameters, the six effects and mono/poly. A
// sundae therefore sounds the same on the hardware, and its voice is an ordinary DX7 patch that
// can be exported as a bank. The front end (screen, panel, state) lives in Swift.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The toppings, in the order they are stacked and numbered below.
enum {
  IS_WHIPPED_CREAM,  // Chorus
  IS_HOT_FUDGE,      // Distortion
  IS_CARAMEL,        // Phaser, plus a worn-tape detune and flutter on the voice
  IS_CHOC_CHIPS,     // Delay
  IS_SPRINKLES,      // Reverb
  IS_TOPPING_COUNT
};

// A sundae is a flat list of numbers, so it is easy to keep and to pass around.
// Toppings take four numbers each: on (0/1), then the three knobs A, B and C.
//   Whipped Cream: A mix, B rate, C depth          Hot Fudge: A gain, B tone, C level
//   Caramel: A drizzle (phaser mix, tape detune and flutter), B rate, C depth
//   Choc Chips: A mix, B feedback, C speed         Sprinkles: A mix, B decay, C room (0 room, 1 hall, 2 plate)
enum {
  IS_FLAVOR,      // 0 .. iscream_flavor_count() - 1
  IS_SCOOPS,      // 1 .. 3: how wide the voice is (detune between its three stacks)
  IS_SWEET,       // 0 .. 100, 50 as designed: brightness (how hard the modulators push)
  IS_MELT,        // 0 .. 100, 50 as designed: how long the sound hangs on
  IS_FIZZ,        // 0 .. 100: vibrato
  IS_CONE_ON,     // 0 / 1
  IS_CONE_STYLE,  // 0 waffle (low pass), 1 sugar (band pass), 2 cup (high pass)
  IS_TOAST,       // 0 .. 107: cutoff, as the FM-1 cutoff CC
  IS_CRUNCH,      // 0 .. 100: resonance
  IS_TOPPING_BASE,
  IS_FIELDS = IS_TOPPING_BASE + 4 * IS_TOPPING_COUNT
};
#define IS_TOP(topping, knob) (IS_TOPPING_BASE + (topping) * 4 + (knob))  // knob 0 = on, 1..3 = A..C

typedef struct {
  char name[11];       // as stored in the voice, 10 characters
  char title[24];      // as shown
  char blurb[40];      // a few words on how it sounds
  uint32_t color;      // 0xRRGGBB scoop colour
  uint32_t accent;     // 0xRRGGBB shade for the shadow side and swirls
  uint32_t bits;       // 0xRRGGBB colour of the chips, specks or swirls in the scoop
  int pattern;         // 0 plain, 1 specks, 2 chunks, 3 swirl, 4 confetti, 5 stripes
  bool mono;
} IScreamFlavor;

int iscream_flavor_count(void);
bool iscream_flavor(int index, IScreamFlavor* out);
// The sundae a flavor is meant to be played as.
void iscream_default_sundae(int flavor, int32_t out[IS_FIELDS]);
// Clamps every field to its range and returns the field's range.
void iscream_clamp(int32_t sundae[IS_FIELDS]);
void iscream_range(int field, int* lo, int* hi);

// Puts the flavors in banks A-D of the engine (the same 32 slots in each, the rest empty voices),
// so program changes, SAVE and bank export all work on them. Call after the engine started.
void iscream_install(void);
// Loads the flavor into the edit buffer, then applies the sundae.
void iscream_load(const int32_t sundae[IS_FIELDS]);
// Writes the sundae to the edit buffer and the effects. Voice changes reach the next note, as on the FM-1.
void iscream_apply(const int32_t sundae[IS_FIELDS]);
// What the FM-1 holds for the sundae's voice: the 155 bytes of DX7 VCED.
void iscream_voice(const int32_t sundae[IS_FIELDS], uint8_t out[155]);
// A DX7 32-voice bank dump (4104 bytes) of the flavors with their signature voices.
void iscream_export_bank(uint8_t out[4104]);

#ifdef __cplusplus
}
#endif
