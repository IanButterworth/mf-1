/* Arpeggi's player for the FM-1: the desktop's player (Sources/ArpeggiCore/player.cc) for the audio interrupt. It
 * runs the desktop's own arp.c on the engine's 64-sample blocks, and presses the engine's keys through three calls
 * its caller provides, which count each note's holders. */
#pragma once
#include <stdint.h>

#include "ArpeggiCore.h"

#define AP_BLOCK 64                    /* samples a call to ap_player_block covers */
#define AP_RATE 44100

/* The main loop's side, with the audio interrupt held off. The pool changes at once; notes start and stop at the
 * start of the next block. As on the desktop: a key that finds the clock stopped starts it, its first note with it;
 * a change keeps the time; a stop lets go of every note the lanes hold and keeps the pool. */
void ap_player_set(const ApProc *p);
void ap_player_mode(int hold, int ring);
void ap_player_key_down(int note);
void ap_player_key_up(int note);
void ap_player_seed(const int8_t *notes, int n);
int ap_player_start(void);             /* 0 when the pool is empty */
void ap_player_stop(void);
void ap_player_clear(void);
int ap_player_now(int64_t *tick);      /* whether the clock runs, and its tick */
void ap_player_pool(ApPool *out);
int ap_player_held(void);              /* the notes waiting for their release */
void ap_player_reset(void);            /* everything forgotten, nothing released: the engine was reset too */

/* The notes the lanes started lately, newest first, with how long ago and how long held, in samples. */
typedef struct {
    uint8_t lane, note;
    uint32_t ago, length;
} ap_hit_t;
int ap_player_recent(ap_hit_t *out, int max);

/* The audio interrupt, before each block of the engine. */
void ap_player_block(void);

/* The caller's: how many hold a note, one more holder (strike: sound it again even if it is held), one fewer (the
 * last lets go of it). */
int ap_refs(int note);
void ap_hold(int note, int strike);
void ap_let_go(int note);
