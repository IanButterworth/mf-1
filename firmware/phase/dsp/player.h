/* Phase's player for the FM-1: the desktop's player (Sources/PhaseCore/player.cc) for the audio interrupt. It runs the
 * desktop's own process.c on the engine's 64-sample blocks, and presses the engine's keys through three calls its
 * caller provides, which count each note's holders. */
#pragma once
#include <stdint.h>

#include "PhaseCore.h"

#define PH_BLOCK 64                    /* samples a call to ph_player_block covers */
#define PH_RATE 44100

/* The main loop's side, with the audio interrupt held off: each takes effect at the start of the next block. A start
 * plays from the beginning; a change keeps the time, and restarts on the next step if the parts move differently, as
 * on the desktop; a stop lets go of every note the process holds. */
void ph_player_start(const PhaseProc *p);
void ph_player_change(const PhaseProc *p);
void ph_player_stop(void);
/* Where the process is (0 before it starts again after a change), and whether it plays; also with the interrupt held
 * off. */
int ph_player_now(int64_t *tick);
int ph_player_held(void);              /* the notes waiting for their release */
void ph_player_reset(void);            /* everything forgotten, nothing released: the engine was reset too */

/* The audio interrupt, before each block of the engine. */
void ph_player_block(void);

/* The caller's: how many hold a note, one more holder (strike: sound it again even if it is held), one fewer (the last
 * lets go of it). */
int ph_refs(int note);
void ph_hold(int note, int strike);
void ph_let_go(int note);
