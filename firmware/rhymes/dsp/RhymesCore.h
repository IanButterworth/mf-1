// Rhymes: nursery rhymes to learn on the keys, for small children.
//
// Each song is a tune with its words, a sound and a picture. The key to play next lights up; a right key plays the
// next note and moves the picture on, and a finished song earns up to three stars. The songs, the game, the sounds
// and the pictures are plain C with no C library, so the FM-1 firmware builds the same files. The sounds are DX7
// voices and the FM-1's effects only, so a song sounds the same on the hardware.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// How much help the lights give. Magic: any key plays the next note. Guide: the lit key plays it and the song waits
// for it. Solo: the light comes on only when the child waits, or after a wrong key.
enum { RH_MAGIC, RH_GUIDE, RH_SOLO, RH_LEVEL_COUNT };
// How the next key shows: blinking, lit steadily, or blinking with the key after it lit dimly.
enum { RH_BLINK, RH_STEADY, RH_AHEAD, RH_LIGHTS_COUNT };

// A song's settings, a flat row of numbers as in the other modes.
enum {
  RH_SONG,    // 0 .. rh_song_count() - 1
  RH_LEVEL,   // RH_MAGIC .. RH_SOLO
  RH_SPEED,   // 50 .. 150: percent of the song's own tempo, when it plays itself
  RH_ROOM,    // 0 .. 100: how much reverb
  RH_LIGHTS,  // RH_BLINK .. RH_AHEAD
  RH_SOUND,   // 0 .. rh_sound_count() - 1: the song's own sound unless changed
  RH_FIELDS
};

#define RH_MAX_NOTES 96
#define RH_MAX_CHORDS 32
#define RH_KEYS 27
// The lowest note the keys play at octave 0 (F3), as on the FM-1.
#define RH_KEY_BASE 53

int rh_song_count(void);
const char *rh_song_title(int song);
// A few words on the song: "Three notes: E, D and C".
const char *rh_song_blurb(int song);
// The sound the song is written for.
int rh_song_sound(int song);
// 0xRRGGBB: the song's colour, for its title and highlights.
uint32_t rh_song_accent(int song);
// The colour a note is shown in, by its pitch class: C red, D orange, E yellow, F green, G teal, A violet, B pink.
uint32_t rh_note_colour(int note);
const char *rh_level_name(int level);
const char *rh_lights_name(int lights);
int rh_sound_count(void);
const char *rh_sound_name(int sound);

void rh_default(int song, int32_t out[RH_FIELDS]);
void rh_range(int field, int *lo, int *hi);
void rh_clamp(int32_t row[RH_FIELDS]);

// A song's tune: each note's pitch (MIDI, at octave 0), length in eighths and line, and where its syllable is in
// the song's words; and its chords: each one's root (0 C .. 11 B, 255 for none), kind and length in eighths.
enum { RH_MAJOR, RH_MINOR, RH_SEVENTH };
typedef struct {
  int song, count, lines, tempo, chords, half;
  uint8_t note[RH_MAX_NOTES], len[RH_MAX_NOTES], line[RH_MAX_NOTES];
  uint16_t at[RH_MAX_NOTES];
  uint8_t width[RH_MAX_NOTES];
  uint8_t chord_root[RH_MAX_CHORDS], chord_kind[RH_MAX_CHORDS], chord_len[RH_MAX_CHORDS];
} rh_tune_t;

// Reads the song's tune; returns its note count, 0 if the song is unknown, or its notes, words and chords disagree.
int rh_tune(int song, rh_tune_t *out);
// The syllable sung on note i, with a hyphen after it when the word goes on: "Twin-", "kle".
void rh_syllable(const rh_tune_t *t, int i, char out[16]);

// The key (0..15 the lower row, 16..26 the upper) that plays a note at octave 0, or -1 if none does.
int rh_key_of(int note);
// The note a key plays at octave 0.
int rh_note_of(int key);

// The 155 bytes of DX7 VCED that are one of the sounds.
void rh_voice(int sound, uint8_t out[155]);
// The effects the row asks for, as FX CC numbers and values: writes up to 24 pairs, returns how many.
int rh_fx(const int32_t row[RH_FIELDS], int cc[24], int value[24]);

// ------------------------------------------------------------------ the game

// Ready to start; being played; just finished (stars and a fanfare); playing itself to be listened to; and the
// finished song played back in full, with its chords, as a reward.
enum { RH_READY, RH_PLAYING, RH_DONE, RH_LISTEN, RH_ENCORE };
// What a key did: played the next note, was wrong (silent, and the song waits), or played its own note freely.
enum { RH_RIGHT, RH_WRONG, RH_FREE };

// Notes the game plays by itself: the song when it is listened to, and the fanfare and the song with its chords when
// it is finished. A note two parts share starts once and stops when both let go.
#define RH_AUTO_MAX 8

typedef struct {
  rh_tune_t tune;
  int level, lights, speed;
  int state, pos;
  int misses, missed_here, streak, best_streak, stars;
  int skipped;  // started part way through, so it earns one star at most
  uint32_t last_right, last_wrong, done_at, listen_at, started;
  // The notes the game has started by itself and not yet let go, and which part each is: tune, chord or fanfare.
  int auto_note[RH_AUTO_MAX], auto_part[RH_AUTO_MAX], auto_count;
  int auto_next;     // the next of the song's or fanfare's notes to start
  int strike_next;   // the next time the chords sound, counted from the first
  uint32_t chord_off;  // when the chord sounding now lets go, from listen_at
} rh_game_t;

// Loads the row's song, ready to play from the start.
void rh_game_init(rh_game_t *g, const int32_t row[RH_FIELDS], uint32_t now);
// Takes a new level, speed or lights without starting again.
void rh_game_settings(rh_game_t *g, const int32_t row[RH_FIELDS]);
// A key pressed: returns RH_RIGHT, RH_WRONG or RH_FREE, and the note to play at octave 0 in *note, -1 for none.
int rh_game_key(rh_game_t *g, int key, uint32_t now, int *note);
// Back to the start of the song.
void rh_game_restart(rh_game_t *g, uint32_t now);
// To the start of a line before or after the one the song is on, to practise it.
void rh_game_line(rh_game_t *g, int delta, uint32_t now);
// The song plays itself, with its keys lit; again stops it. A finished song plays itself again with its chords.
void rh_game_listen(rh_game_t *g, uint32_t now);
// Moves time on: a finished song goes back to the start after its celebration, a listened song ends. Writes the
// notes to let go and then to start, at octave 0, and returns how many of each through *offs and *ons.
void rh_game_step(rh_game_t *g, uint32_t now, int off[RH_AUTO_MAX], int *offs, int on[RH_AUTO_MAX], int *ons);
// When rh_game_step next has something to do, or now + 1000 when nothing is due.
uint32_t rh_game_due(const rh_game_t *g, uint32_t now);
// The white keys the screen's little keyboard shows for a tune: at least eight, round the ones the tune uses.
void rh_key_window(const rh_tune_t *t, int *first, int *count);
// The note the song is on: the next to play, or the one sounding while it plays itself; -1 when finished.
int rh_game_current(const rh_game_t *g, uint32_t now);
// The keys to light, a bit each: lit fully, and lit dimly.
void rh_game_lights(const rh_game_t *g, uint32_t now, uint32_t *bright, uint32_t *dim);
// Stars for a finished song, 1..3, by how few wrong keys it took. Magic earns one.
int rh_game_stars(const rh_game_t *g);

// ------------------------------------------------------------------ the picture

// What the picture shows of the game. Frames are scene frames, ten a second.
typedef struct {
  int pos, count;          // notes played so far, of how many
  int line, lines;         // the line the next note is on, of how many
  int line_pos, line_len;  // notes played so far in that line, of how many it has
  int hit;                 // frames since the last right note, 1000 if none
  int miss;                // frames since the last wrong key, 1000 if none
  int done;                // frames since the song was finished, -1 while it is not
  int listen;              // the song is playing itself
  int sounding;            // a note sounds now
} rh_view_t;

#define RH_SCENE_W 120
#define RH_SCENE_H 64
#define RH_SCENE_MS 100
// Fills the view from the game at a time.
void rh_game_view(const rh_game_t *g, uint32_t now, int sounding, rh_view_t *v);
// The song's picture, RH_SCENE_W by RH_SCENE_H pixels as 0xRRGGBB, row by row. `step` counts scene frames.
void rh_scene(int song, const rh_view_t *v, int step, uint32_t out[RH_SCENE_W * RH_SCENE_H]);

// ------------------------------------------------------------------ MF-1 (the desktop only)

// Puts the songs' sounds in banks A-D of the engine, the same 32 slots in each. Call after the engine started.
void rhymes_install(void);
// Loads the song's sound and effects, and the game for it.
void rhymes_load(const int32_t row[RH_FIELDS]);
// The effects and the game's settings again, after a change to the row.
void rhymes_apply(const int32_t row[RH_FIELDS]);
// A DX7 32-voice bank dump (4104 bytes) of the songs' sounds.
void rhymes_export_bank(uint8_t out[4104]);
// The keys of the panel. A key plays its note through the engine at the current octave, and returns what it did.
int rhymes_key_down(int key);
void rhymes_key_up(int key);
void rhymes_all_keys_up(void);
void rhymes_restart(void);
void rhymes_line(int delta);
void rhymes_listen(void);
// A copy of the game and the time, for drawing.
void rhymes_game(rh_game_t *out, uint32_t *now);
// Changes each time the game moves on, so the screen knows to redraw.
uint32_t rhymes_generation(void);
// Offline, for tests: the game's clock is set by hand, and rhymes_offline_step plays what falls due.
void rhymes_offline(int on);
void rhymes_offline_step(uint32_t now);

#ifdef __cplusplus
}
#endif
