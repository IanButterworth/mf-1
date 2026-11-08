/* What a mode is to the shared core: its items (months, dogs) as rows of numbers, how a row becomes a voice, effects,
 * an arpeggio and the notes a key plays, its four knob pages, its buttons and its screen. The core owns the engine,
 * the keys, the arpeggiator, saving, the panel and the shared drawing; a mode adds only what is its own. */
#pragma once
#include <stdint.h>
#include "fx.h"

#define MD_FIELDS 32                   /* the most fields a mode's row may have */
#define MD_PAGES 7                     /* the most pages: one for each of the top row's buttons and HOME */
#define MD_ITEMS 16                    /* the most items a mode may have */

/* How repeats, chases and patterns run on the arpeggiator: MF-1's mode numbering and rate index, gate in percent,
 * tempo in BPM. */
typedef struct {
    int on, mode, octaves, rate, gate, tempo;
} md_pattern_t;

/* FMT_NUM shows the number; the others are the mode's own, which its format() writes. */
enum { FMT_NUM };

typedef struct {
    const char *name;
    int8_t field;
    uint8_t fmt;
} md_param_t;

/* The colours of the shared tabs, value chips and pop-up. */
typedef struct {
    uint32_t bg, ink, faded, tab_bg, tab_ink, focus_bg, focus_ink, chip_bg, chip_frame, chip_ink, bar_off;
    uint32_t pill_bg, pill_ink;
    uint8_t dots;                      /* value bars as round spots (else blocks) */
    uint8_t tab_frame;                 /* tabs outlined in ink */
} md_theme_t;

typedef struct {
    const char *name;                  /* on the chooser */
    const char *reset_words;           /* "as composed": the pop-up after RESET */
    const char *reset_pill;            /* what that pop-up says instead, if set ("Fresh scoop") */
    uint32_t obj, magic;               /* the store object the mode's rows live in, and its magic */
    uint8_t version;                   /* the rows' format: a saved copy of another version is not loaded */
    int fields;
    int npages;                        /* pages on HOME, FX, SEL, ENV, LFO, EDIT and GLO, in that order */
    const char *page_title[MD_PAGES];
    md_param_t params[MD_PAGES][4];
    int algo_field;                    /* what the ALGORITHM knob turns, its name and format */
    const char *algo_name;
    uint8_t algo_fmt;
    const md_theme_t *theme;
    uint8_t *extra;                    /* a few bytes of the mode's own state, saved with its rows */
    uint8_t extra_len;

    int (*count)(void);
    void (*defaults)(int item, int32_t *out);
    void (*range)(int field, int *lo, int *hi);
    void (*clamp)(int32_t *row);
    void (*voice)(const int32_t *row, uint8_t v[155]);
    void (*fx)(const int32_t *row, isc_fx_params_t *p);
    void (*pattern)(const int32_t *row, md_pattern_t *p);
    int (*key_notes)(const int32_t *row, int note, int left, int out[8]);
    int (*mono)(const int32_t *row);
    int (*releases)(int field);        /* a change to this field lets go of the held keys (a playing style) */
    void (*format)(int fmt, int v, char *out);
    const char *(*item_name)(int item);
    void (*press)(uint32_t button);    /* the top row's buttons that are not pages */
    void (*repress)(int page);         /* the button of the page already showing, pressed again */
    int (*knob)(int k, int delta);     /* a turn of KNOB1-4 the mode handles itself: nonzero if it did */
    uint32_t (*dim)(void);             /* buttons it lights dimly */
    void (*frame)(uint32_t now);       /* every pass of the main loop, after the input */
    void (*selected)(void);            /* after the item changes */
    void (*scene)(uint32_t now, uint32_t *px);    /* draws the picture into the shared buffer, if the mode has one */
    int scene_w, scene_h, art_k;
    void (*draw)(uint32_t now);        /* each screen band: what goes over and around the picture, or everything */
    uint16_t idle_ms, busy_ms;         /* how often the screen moves, and while a note sounds */
    int (*expect)(const char *what, int *got);    /* the host simulator's checks the mode adds */
} md_mode_t;
