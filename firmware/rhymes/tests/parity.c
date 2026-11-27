/* Plays every song through the game as a child might, and draws and voices it, folding everything that comes out into
 * one number: the tunes, the voices and effects, the notes a key and a listened song play, the lights and the pictures.
 * run_parity.sh builds it unoptimised and optimised; the two must print the same. */
#include <stdio.h>

#include "RhymesCore.h"

static uint32_t sum = 2166136261u;

static void fold(uint32_t v)
{
    sum = (sum ^ v) * 16777619u;
}

int main(void)
{
    static uint32_t px[RH_SCENE_W * RH_SCENE_H];
    int s, i, notes = 0;
    for (s = 0; s < rh_sound_count(); s++) {
        uint8_t v[155];
        rh_voice(s, v);
        for (i = 0; i < 155; i++)
            fold(v[i]);
    }
    for (s = 0; s < rh_song_count(); s++) {
        int32_t row[RH_FIELDS];
        int cc[24], val[24], n, level, note, off[RH_AUTO_MAX], on[RH_AUTO_MAX], no, nn;
        rh_game_t g;
        rh_view_t v;
        uint32_t t, end, bright, dim;
        rh_default(s, row);
        for (row[RH_ROOM] = 0; row[RH_ROOM] <= 100; row[RH_ROOM] += 25) {
            n = rh_fx(row, cc, val);
            for (i = 0; i < n; i++)
                fold((uint32_t)(cc[i] << 8 | val[i]));
        }
        for (level = 0; level < RH_LEVEL_COUNT; level++) {
            row[RH_LEVEL] = level;
            row[RH_LIGHTS] = level;
            rh_game_init(&g, row, 1000);
            notes += g.tune.count;
            /* every third note a wrong key first, then the right one, then lights and a picture */
            for (i = 0, t = 1000; i < g.tune.count; i++) {
                int key = rh_key_of(g.tune.note[i]);
                t += 700u + (uint32_t)(i % 5) * 400u;
                if (i % 3 == 0)
                    fold((uint32_t)rh_game_key(&g, key == 0 ? 1 : 0, t - 300u, &note) << 8 | (uint32_t)(note & 0xFF));
                fold((uint32_t)rh_game_key(&g, key, t, &note) << 8 | (uint32_t)(note & 0xFF));
                rh_game_lights(&g, t + 250u, &bright, &dim);
                fold(bright), fold(dim);
                if (i % 7 == 0) {
                    rh_game_view(&g, t + 120u, 1, &v);
                    rh_scene(s, &v, i, px);
                    for (n = 0; n < RH_SCENE_W * RH_SCENE_H; n++)
                        fold(px[n]);
                }
            }
            fold((uint32_t)g.stars);
            /* the celebration: its fanfare, the song again with its chords, and the song ready again */
            for (end = t + 120000u; t < end && (g.state == RH_DONE || g.state == RH_ENCORE); t += 7u) {
                rh_game_step(&g, t, off, &no, on, &nn);
                for (i = 0; i < no; i++)
                    fold(0x1000u | (uint32_t)off[i]);
                for (i = 0; i < nn; i++)
                    fold(0x2000u | (uint32_t)on[i]);
                rh_game_lights(&g, t, &bright, &dim);
                fold(bright);
            }
            rh_game_view(&g, t, 0, &v);
            rh_scene(s, &v, 5, px);
            for (n = 0; n < RH_SCENE_W * RH_SCENE_H; n++)
                fold(px[n]);
        }
        rh_game_listen(&g, t);
        for (; g.state == RH_LISTEN; t += 3u) {
            rh_game_step(&g, t, off, &no, on, &nn);
            for (i = 0; i < nn; i++)
                fold((uint32_t)on[i] << 16 | (t & 0xFFFFu));
        }
    }
    printf("%d songs, %d notes played, digest %08x\n", rh_song_count(), notes, sum);
    return 0;
}
