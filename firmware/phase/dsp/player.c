/* Phase's player, run by the audio interrupt: the desktop's player.cc with its thread and clock replaced by the
 * engine's blocks. The ticks are counted from the samples with an exact remainder, so the process keeps the desktop's
 * time however long it plays. Notes and releases are due at ticks; everything due within a block is done at the
 * block's start, in the desktop's order, so a note sounds at most one block (1.45 ms) early and every part alike. */
#include "player.h"

/* As the desktop's: the most notes waiting for their release, and how close two starts of a note may be and still
 * count as one stroke (15 ms). */
#define PH_OFFS 64
#define PH_UNISON 662u

enum { PH_NONE, PH_START, PH_CHANGE, PH_STOP };

static struct {
    PhaseProc proc;
    int playing;
    int64_t tick;                      /* the process's tick at the start of the next block */
    int64_t scan;                      /* notes before this tick have been started */
    uint32_t frac;                     /* what is left over of a tick, in 44100ths */
    uint32_t clock;                    /* samples since the start, at the start of the next block */
    int noffs;
    struct {
        int64_t due;                   /* the tick it is released at */
        uint8_t note;
    } off[PH_OFFS];
    uint32_t last_on[128];
    /* the main loop's last command, taken at the next block */
    uint8_t cmd;
    PhaseProc next;
} pl;

void ph_player_start(const PhaseProc *p)
{
    pl.cmd = PH_START;
    pl.next = *p;
}

/* A change after a start not yet taken is still a start. */
void ph_player_change(const PhaseProc *p)
{
    if (pl.cmd != PH_START && pl.cmd != PH_STOP)
        pl.cmd = PH_CHANGE;
    pl.next = *p;
}

void ph_player_stop(void) { pl.cmd = PH_STOP; }

int ph_player_now(int64_t *tick)
{
    *tick = pl.tick > 0 ? pl.tick : 0;
    return pl.playing;
}

int ph_player_held(void) { return pl.noffs; }

void ph_player_reset(void)
{
    pl.playing = 0;
    pl.noffs = 0;
    pl.cmd = PH_NONE;
}

/* Ticks at the process's tempo as samples: a tick is 1/(64 * tempo) of a second. */
static uint32_t ph_samples(int32_t ticks) { return (uint32_t)ticks * 11025u / (16u * (uint32_t)pl.proc.tempo); }

static void ph_fire_off(int i)
{
    int note = pl.off[i].note;
    pl.off[i] = pl.off[--pl.noffs];
    ph_let_go(note);
}

static int ph_soonest_off(void)
{
    int best = -1, i;
    for (i = 0; i < pl.noffs; i++)
        if (best < 0 || pl.off[i].due < pl.off[best].due)
            best = i;
    return best;
}

static void ph_fire_on(const PhaseNote *n, uint32_t at)
{
    int strike = ph_refs(n->note) == 0 || at - pl.last_on[n->note] >= PH_UNISON;
    if (strike)
        pl.last_on[n->note] = at;
    ph_hold(n->note, strike);
    if (pl.noffs == PH_OFFS)
        ph_fire_off(ph_soonest_off());
    pl.off[pl.noffs].due = n->tick + n->length;
    pl.off[pl.noffs].note = (uint8_t)n->note;
    pl.noffs++;
}

static void ph_stop_notes(void)
{
    pl.playing = 0;
    while (pl.noffs)
        ph_fire_off(pl.noffs - 1);
}

static int64_t ph_floor_mod(int64_t a, int64_t b)
{
    int64_t r = a % b;
    return r < 0 ? r + b : r;
}

static void ph_take_command(void)
{
    int i;
    switch (pl.cmd) {
    case PH_START:
        ph_stop_notes();
        pl.proc = pl.next;
        pl.tick = pl.scan = 0;
        pl.frac = 0;
        pl.playing = 1;
        break;
    case PH_CHANGE:
        /* A change to how the parts move starts again from unison on part 1's next step, so the beat carries on; a
         * tempo change keeps the time, as the ticks go on from where they are. */
        if (pl.playing && !phase_proc_same_motion(&pl.next, &pl.proc)) {
            int64_t shift = pl.tick - ph_floor_mod(pl.tick, PH_TPS) + PH_TPS;
            pl.tick -= shift;
            pl.scan = 0;
            for (i = 0; i < pl.noffs; i++)
                pl.off[i].due -= shift;
        }
        /* The notes already sounding keep their length in time, as the desktop's do. */
        if (pl.next.tempo != pl.proc.tempo)
            for (i = 0; i < pl.noffs; i++) {
                int32_t left = (int32_t)(pl.off[i].due - pl.tick);
                if (left > 0)
                    pl.off[i].due = pl.tick + left * pl.next.tempo / pl.proc.tempo;
            }
        pl.proc = pl.next;
        break;
    case PH_STOP: ph_stop_notes(); break;
    }
    pl.cmd = PH_NONE;
}

void ph_player_block(void)
{
    const uint32_t end_clock = pl.clock + PH_BLOCK;
    int64_t end = pl.tick;
    PhaseNote due[PH_MAX_PARTS];
    int ndue = 0, i;
    ph_take_command();
    if (pl.playing) {
        uint32_t f = pl.frac + 4096u * (uint32_t)pl.proc.tempo;
        end = pl.tick + (int64_t)(f / PH_RATE);
        pl.frac = f % PH_RATE;
    }
    /* Releases and notes in time order, releases first at the same sample, as the desktop's player fires them. */
    for (;;) {
        int oi;
        uint32_t on_at = 0;
        int off_due, on_due;
        if (pl.playing && !ndue && pl.scan < end) {
            ndue = phase_next_notes(&pl.proc, pl.scan, end, due);
            if (!ndue)
                pl.scan = end;
        }
        oi = ph_soonest_off();
        off_due = oi >= 0 && pl.off[oi].due < end;
        on_due = ndue > 0;
        if (!off_due && !on_due)
            break;
        if (off_due && (!on_due || pl.off[oi].due <= due[0].tick)) {
            ph_fire_off(oi);
        } else {
            on_at = pl.clock + ph_samples((int32_t)(due[0].tick - pl.tick));
            for (i = 0; i < ndue; i++)
                ph_fire_on(&due[i], on_at);
            pl.scan = due[0].tick + 1;
            ndue = 0;
        }
    }
    if (pl.playing)
        pl.tick = end;
    pl.clock = end_clock;
}
