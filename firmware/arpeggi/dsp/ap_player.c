/* Arpeggi's player, run by the audio interrupt: the desktop's player.cc with its thread and clock replaced by the
 * engine's blocks. The ticks are counted from the samples with an exact remainder, so the lanes keep the desktop's
 * time however long they play. Everything due within a block is done at the block's start, releases first at the
 * same tick, so a note sounds at most one block (1.45 ms) early and both lanes alike. */
#include "ap_player.h"

/* As the desktop's: the most notes waiting for their release, how close two starts of a note may be and still count
 * as one strike (8 ms), and how many recent notes are kept for the screen. */
#define AP_OFFS 96
#define AP_UNISON 353u
#define AP_RECENT 32

static struct {
    ApProc proc;
    ApPool pool;
    int playing;
    uint8_t restart, release;          /* for the next block: start the clock again, let go of every note */
    int64_t tick;                      /* the clock's tick at the start of the next block */
    int64_t scan;                      /* notes before this tick have been started */
    uint32_t frac;                     /* what is left over of a tick, in 44100ths */
    uint32_t clock;                    /* samples since the start, at the start of the next block */
    int noffs;
    struct {
        int64_t due;                   /* the tick it is released at */
        uint8_t note;
    } off[AP_OFFS];
    uint32_t last_on[128];
    struct {
        uint32_t at, length;
        uint8_t lane, note;
    } hit[AP_RECENT];
    int nhit, next_hit;
} pl;

static ApProc pl_was;                  /* the settings the last block ran on */

/* The pool changed: with every key up in Hold, the clock stops, and notes already playing run out by themselves. */
static void ap_pool_changed(void)
{
    if (pl.playing && pl.pool.count == 0)
        pl.playing = 0;
}

static void ap_start_clock(void)
{
    pl.playing = 1;
    pl.restart = 1;
}

void ap_player_set(const ApProc *p) { pl.proc = *p; }

void ap_player_mode(int hold, int ring)
{
    if (pl.pool.hold == hold && pl.pool.ring == ring)
        return;
    ap_pool_mode(&pl.pool, hold, ring);
    ap_pool_changed();
}

void ap_player_key_down(int note)
{
    if (note < 0 || note > 127)
        return;
    ap_pool_key_down(&pl.pool, note);
    ap_pool_changed();
    if (!pl.playing && pl.pool.count > 0)
        ap_start_clock();
}

void ap_player_key_up(int note)
{
    if (note < 0 || note > 127)
        return;
    ap_pool_key_up(&pl.pool, note);
    ap_pool_changed();
}

void ap_player_seed(const int8_t *notes, int n)
{
    ap_pool_seed(&pl.pool, notes, n);
    ap_pool_changed();
    if (!pl.playing && pl.pool.count > 0)
        ap_start_clock();
}

int ap_player_start(void)
{
    ap_player_stop();
    if (pl.pool.count == 0)
        return 0;
    ap_start_clock();
    return 1;
}

void ap_player_stop(void)
{
    pl.playing = 0;
    pl.restart = 0;
    pl.release = 1;
}

void ap_player_clear(void)
{
    ap_player_stop();
    ap_pool_clear(&pl.pool);
}

int ap_player_now(int64_t *tick)
{
    *tick = pl.restart ? 0 : pl.tick;
    return pl.playing;
}

void ap_player_pool(ApPool *out) { *out = pl.pool; }
int ap_player_held(void) { return pl.noffs; }

void ap_player_reset(void)
{
    ApProc p = pl.proc;
    int i;
    for (i = 0; i < (int)sizeof pl; i++)
        ((uint8_t *)&pl)[i] = 0;
    pl_was.tempo = 0;
    ap_pool_init(&pl.pool, AP_LATCH, 4);
    pl.proc = p;
    if (pl.proc.tempo <= 0)
        pl.proc.tempo = 120;
}

int ap_player_recent(ap_hit_t *out, int max)
{
    int n = 0;
    while (n < pl.nhit && n < max) {
        int i = (pl.next_hit - 1 - n + AP_RECENT) % AP_RECENT;
        out[n].lane = pl.hit[i].lane;
        out[n].note = pl.hit[i].note;
        out[n].ago = pl.clock - pl.hit[i].at;
        out[n].length = pl.hit[i].length;
        n++;
    }
    return n;
}

/* Ticks at the lanes' tempo as samples: a tick is 1/(56 * tempo) of a second. */
static uint32_t ap_samples(int64_t ticks)
{
    if (ticks <= 0)
        return 0;
    if (ticks > 2000000)
        ticks = 2000000;
    return (uint32_t)ticks * 1575u / (2u * (uint32_t)pl.proc.tempo);
}

static void ap_fire_off(int i)
{
    int note = pl.off[i].note;
    pl.off[i] = pl.off[--pl.noffs];
    ap_let_go(note);
}

static int ap_soonest_off(void)
{
    int best = -1, i;
    for (i = 0; i < pl.noffs; i++)
        if (best < 0 || pl.off[i].due < pl.off[best].due)
            best = i;
    return best;
}

static void ap_fire_on(const ApNote *n, uint32_t at)
{
    int strike = ap_refs(n->note) == 0 || at - pl.last_on[n->note] >= AP_UNISON, h;
    if (strike)
        pl.last_on[n->note] = at;
    ap_hold(n->note, strike);
    if (pl.noffs == AP_OFFS)
        ap_fire_off(ap_soonest_off());
    pl.off[pl.noffs].due = n->tick + n->length;
    pl.off[pl.noffs].note = (uint8_t)n->note;
    pl.noffs++;
    h = pl.next_hit;
    pl.hit[h].at = at;
    pl.hit[h].length = ap_samples(n->length);
    pl.hit[h].lane = (uint8_t)n->lane;
    pl.hit[h].note = (uint8_t)n->note;
    pl.next_hit = (h + 1) % AP_RECENT;
    if (pl.nhit < AP_RECENT)
        pl.nhit++;
}

void ap_player_block(void)
{
    const uint32_t end_clock = pl.clock + AP_BLOCK;
    int64_t end = pl.tick;
    ApNote due[AP_MAX_OUT];
    int ndue = 0, i;
    if (pl.release) {
        while (pl.noffs)
            ap_fire_off(pl.noffs - 1);
        pl.release = 0;
    }
    /* The notes already sounding keep their length in time, as the desktop's do. */
    if (pl_was.tempo > 0 && pl.proc.tempo != pl_was.tempo)
        for (i = 0; i < pl.noffs; i++) {
            int64_t left = pl.off[i].due - pl.tick;
            if (left > 0)
                pl.off[i].due = pl.tick + (int32_t)left * pl.proc.tempo / pl_was.tempo;
        }
    pl_was = pl.proc;
    if (pl.restart) {
        /* the clock starts at tick 0 now; notes still waiting for their release keep their time */
        for (i = 0; i < pl.noffs; i++)
            pl.off[i].due -= pl.tick;
        pl.tick = pl.scan = end = 0;
        pl.frac = 0;
        pl.restart = 0;
    }
    if (pl.playing || pl.noffs) {
        uint32_t f = pl.frac + 3584u * (uint32_t)pl.proc.tempo;
        end = pl.tick + (int64_t)(f / AP_RATE);
        pl.frac = f % AP_RATE;
    }
    /* Releases and notes in time order, releases first at the same tick, as the desktop's player fires them. */
    for (;;) {
        int oi, off_due, on_due;
        if (pl.playing && !ndue && pl.scan < end) {
            ndue = ap_next_notes(&pl.proc, &pl.pool, pl.scan, end, due);
            if (!ndue)
                pl.scan = end;
        }
        oi = ap_soonest_off();
        off_due = oi >= 0 && pl.off[oi].due < end;
        on_due = ndue > 0;
        if (!off_due && !on_due)
            break;
        if (off_due && (!on_due || pl.off[oi].due <= due[0].tick)) {
            ap_fire_off(oi);
        } else {
            uint32_t on_at = pl.clock + ap_samples(due[0].tick - pl.tick);
            for (i = 0; i < ndue; i++)
                ap_fire_on(&due[i], on_at);
            pl.scan = due[0].tick + 1;
            ndue = 0;
        }
    }
    pl.tick = end;
    pl.clock = end_clock;
}
