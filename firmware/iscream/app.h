/* iScream on the FM-1: the app's settings and its half of the contract with fm1-base. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "ISCREAM"
#define APP_SPLASH_RGB RGB(255, 95, 174)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
/* The screen is drawn in bands this tall, so the canvas leaves the RAM pool to the effects. */
#define APP_CANVAS_PX (240u * 80u)

enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator's checks: flavor, focus, scoops, sweet, voices, octave */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
