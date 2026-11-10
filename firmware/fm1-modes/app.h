/* MF-1's modes on the FM-1 in one firmware: the app's settings and its half of the contract with fm1-base. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "MF-1 MODES"
#define APP_SPLASH_RGB RGB(244, 240, 230)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
/* The screen is drawn in bands this tall, so the canvas leaves the RAM pool to the effects. */
#define APP_CANVAS_PX (240u * 80u)
#define APP_BOOT                         /* the mode is chosen once the keys are scanned */

/* The settings (which mode ran last), the platform's panel calibration, then one object per mode. A new mode's object
 * goes on the end, so every object keeps its number from one release to the next. */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_ALMANAC, OBJ_WOOF, OBJ_ISCREAM, OBJ_PHASE, OBJ_PIANO, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDA000u, 0xD8000u, 0xD6000u, 0xD4000u, 0xD2000u}

void app_init(void);
void app_boot(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator's checks: chooser, mode, item, page, voices, notes, arp, arp_notes, octave, battery, low_battery,
 * dirty, and each mode's own (iScream: flavor, focus, scoops, sweet, cone; Almanac: month, sky, play, mood; Woof: dog, place, play, pack, show;
 * Phase: piece, process, parts, length, transpose, playing, writes, writing, cell, held, releases) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
