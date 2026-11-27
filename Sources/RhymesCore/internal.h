/* What Rhymes' C files share. */
#pragma once
#include "RhymesCore.h"

/* The sounds, one for each song. */
enum {
    RH_SOUND_XYLO, RH_SOUND_KALIMBA, RH_SOUND_FLUTE, RH_SOUND_MUSICBOX, RH_SOUND_MARIMBA, RH_SOUND_BELL,
    RH_SOUND_TOYPIANO, RH_SOUND_BANJO, RH_SOUND_HARP, RH_SOUND_SLEIGH, RH_SOUND_CALLIOPE, RH_SOUND_COUNT
};

typedef struct {
    const char *title, *blurb;
    uint32_t accent;
    int tempo;  /* quarter notes a minute */
    int tonic;  /* the note the fanfare is built on */
    int sound;
    int half;   /* eighths to half a bar: the chords sound again this often */
    const char *notes, *words, *chords;
} rh_song_def_t;

const rh_song_def_t *rh_song_def(int song);
int rh_clampi(int v, int lo, int hi);
