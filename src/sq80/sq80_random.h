/* SPDX-License-Identifier: MIT
 * New programs at random, from the one given: the FM-1's HOME RANDOM screen and the editor page's Randomize share
 * this. AMOUNT (5..100) is how far each value moves (100: anywhere in its range; a switch, a source or a wave changes
 * with that chance); PART which values (SQ80_RP_ALL, OSC, FILTER, ENV, LFO, MOD: the modulation routings). The
 * volume and the voice modes stay; OSC 1 sounds, the filter is not shut, ENV 4 (the amplitude) opens and does not
 * take forever, so a new program is never silent. It is named RND001 .. RND999 from SEQ. Header only (static). */
#ifndef SQ80_RANDOM_H
#define SQ80_RANDOM_H

#include "sq80_params.h"

enum { SQ80_RP_ALL, SQ80_RP_OSC, SQ80_RP_FILTER, SQ80_RP_ENV, SQ80_RP_LFO, SQ80_RP_MOD, SQ80_RP_N };

static uint32_t sq80_rnd(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x2545F491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
static int32_t sq80_rnd_in(uint32_t *s, int32_t lo, int32_t hi)
{
    return lo + (int32_t)(sq80_rnd(s) % (uint32_t)(hi - lo + 1));
}

static uint32_t sq80_rnd_part_of(int id)     /* which SQ80_RP_* a value is (SQ80_RP_N: never changed) */
{
    if (id < SQ80_PARAM_LFO(0, 0)) {
        return SQ80_RP_ENV;
    } else if (id < SQ80_PARAM_OSC(0, 0)) {
        return (id - SQ80_PARAM_LFO(0, 0)) % SQ80_P_LFO_N == SQ80_P_LFO_MOD ? SQ80_RP_MOD : SQ80_RP_LFO;
    } else if (id < SQ80_PARAM_PROG(0)) {
        int q = (id - SQ80_PARAM_OSC(0, 0)) % SQ80_P_OSC_N;
        return q == SQ80_P_OSC_MOD1 || q == SQ80_P_OSC_DEPTH1 || q == SQ80_P_OSC_MOD2 || q == SQ80_P_OSC_DEPTH2 ||
               q == SQ80_P_OSC_DMOD1 || q == SQ80_P_OSC_DDEPTH1 || q == SQ80_P_OSC_DMOD2 || q == SQ80_P_OSC_DDEPTH2
                   ? SQ80_RP_MOD : SQ80_RP_OSC;
    }
    switch (id - SQ80_PARAM_PROG(0)) {
    case SQ80_P_FREQ: case SQ80_P_RES: case SQ80_P_KEYBD: return SQ80_RP_FILTER;
    case SQ80_P_FMOD1: case SQ80_P_FDEPTH1: case SQ80_P_FMOD2: case SQ80_P_FDEPTH2:
    case SQ80_P_PANMOD: case SQ80_P_PANDEPTH: return SQ80_RP_MOD;
    case SQ80_P_PAN: case SQ80_P_SYNC: case SQ80_P_AM: return SQ80_RP_OSC;
    default: return SQ80_RP_N;               /* FINAL, MONO, GLIDE, VC, the restarts, CYC, the name */
    }
}

static void sq80_rnd_at_least(uint8_t *prog, int id, int v)
{
    if (sq80_param_get(prog, id) < v)
        sq80_param_set(prog, id, v);
}
static void sq80_rnd_at_most(uint8_t *prog, int id, int v)
{
    if (sq80_param_get(prog, id) > v)
        sq80_param_set(prog, id, v);
}

static void sq80_randomize(uint8_t *prog, int amt, uint32_t part, uint32_t seq, uint32_t *s)
{
    int id, i;
    char nm[6];
    for (id = 0; id < SQ80_PARAMS; id++) {
        const sq80_param_info *in = sq80_param(id);
        uint32_t pt = sq80_rnd_part_of(id);
        int v = sq80_param_get(prog, id), lo = in->min, hi = in->max;
        if (pt == SQ80_RP_N || (part != SQ80_RP_ALL && pt != part))
            continue;
        if (id >= SQ80_PARAM_OSC(0, 0) && id < SQ80_PARAM_PROG(0) &&
            (id - SQ80_PARAM_OSC(0, 0)) % SQ80_P_OSC_N == SQ80_P_OSC_OCT) {
            lo = -2;                         /* (octaves: the playable middle) */
            hi = 2;
        }
        if (in->kind == SQ80_K_NUM || in->kind == SQ80_K_SIGNED) {
            int span = (hi - lo) * amt / 100;
            v = amt >= 100 ? sq80_rnd_in(s, lo, hi) : v + sq80_rnd_in(s, -span, span);
        } else if ((int32_t)(sq80_rnd(s) % 100u) < amt) {   /* a switch, a source, a wave: changed or not */
            v = sq80_rnd_in(s, lo, hi);
        }
        sq80_param_set(prog, id, v < lo ? lo : v > hi ? hi : v);
    }
    sq80_param_set(prog, SQ80_PARAM_OSC(0, SQ80_P_OSC_OUTPUT), 1);
    sq80_rnd_at_least(prog, SQ80_PARAM_OSC(0, SQ80_P_OSC_LEVEL), 40);
    sq80_rnd_at_least(prog, SQ80_PARAM_PROG(SQ80_P_FREQ), 40);
    sq80_rnd_at_least(prog, SQ80_PARAM_ENV(3, SQ80_P_ENV_L1), 40);
    sq80_rnd_at_least(prog, SQ80_PARAM_ENV(3, SQ80_P_ENV_L2), 30);
    sq80_rnd_at_least(prog, SQ80_PARAM_ENV(3, SQ80_P_ENV_L3), 20);
    sq80_rnd_at_least(prog, SQ80_PARAM_ENV(3, SQ80_P_ENV_LV), 20);
    sq80_rnd_at_most(prog, SQ80_PARAM_ENV(3, SQ80_P_ENV_T1), 45);
    sq80_rnd_at_most(prog, SQ80_PARAM_ENV(3, SQ80_P_ENV_T4), 50);
    nm[0] = 'R'; nm[1] = 'N'; nm[2] = 'D';
    nm[3] = (char)('0' + seq / 100u % 10u);
    nm[4] = (char)('0' + seq / 10u % 10u);
    nm[5] = (char)('0' + seq % 10u);
    for (i = 0; i < 6; i++)
        sq80_param_set(prog, SQ80_PARAM_PROG(SQ80_P_NAME0 + i), nm[i]);
}

#endif
