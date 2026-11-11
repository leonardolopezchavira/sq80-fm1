/* SPDX-License-Identifier: MIT
 * The program's values in place (sq80_params.h); the byte map is sq80_decode_patch's (docs/patch-format.md). */
#include "sq80_params.h"

#include <stddef.h>

/* how a value sits in its byte(s) */
enum {
    F_BITS,    /* an unsigned field: (b >> shift) & mask */
    F_HALF,    /* a signed byte, halved (levels and depths: -63..63) */
    F_S7,      /* the low 7 bits, signed (the filter's and pan's depths) */
    F_OCT,     /* the low 7 bits are 12 x (octave + 3) + semitone */
    F_SEMI,
    F_LFOMOD,  /* bits 7-6 of this byte, then of the next: the LFO's modulation source */
    F_CHAR,
    F_LINK     /* bit 7 on, bits 0-6 a program's place: 0 OFF, else place + 1 (the place kept while off) */
};

typedef struct {
    uint8_t off, f, shift, width;
} place;

static const sq80_param_info ENV_I[SQ80_P_ENV_N] = {
    {"L1", -63, 63, SQ80_K_SIGNED}, {"L2", -63, 63, SQ80_K_SIGNED}, {"L3", -63, 63, SQ80_K_SIGNED},
    {"LV", 0, 63, SQ80_K_NUM},      {"T1", 0, 63, SQ80_K_NUM},      {"T2", 0, 63, SQ80_K_NUM},
    {"T3", 0, 63, SQ80_K_NUM},      {"T4", 0, 63, SQ80_K_NUM},      {"T1V", 0, 63, SQ80_K_NUM},
    {"TK", 0, 63, SQ80_K_NUM},      {"2ND REL", 0, 1, SQ80_K_ONOFF}};
static const place ENV_P[SQ80_P_ENV_N] = {
    {0, F_HALF, 0, 0}, {1, F_HALF, 0, 0}, {2, F_HALF, 0, 0}, {7, F_BITS, 2, 6}, {3, F_BITS, 0, 6},
    {4, F_BITS, 0, 6}, {5, F_BITS, 0, 6}, {6, F_BITS, 0, 6}, {8, F_BITS, 0, 6}, {9, F_BITS, 0, 6},
    {6, F_BITS, 7, 1}};

static const sq80_param_info LFO_I[SQ80_P_LFO_N] = {
    {"FREQ", 0, 63, SQ80_K_NUM}, {"WAVE", 0, 3, SQ80_K_LFOWAVE}, {"L1", 0, 63, SQ80_K_NUM},
    {"L2", 0, 63, SQ80_K_NUM},   {"DELAY", 0, 63, SQ80_K_NUM},   {"MOD", 0, 15, SQ80_K_SOURCE},
    {"HUMAN", 0, 1, SQ80_K_ONOFF}, {"RESET", 0, 1, SQ80_K_ONOFF}};
static const place LFO_P[SQ80_P_LFO_N] = {
    {0, F_BITS, 0, 6}, {0, F_BITS, 6, 2}, {1, F_BITS, 0, 6}, {2, F_BITS, 0, 6},
    {3, F_BITS, 0, 6}, {1, F_LFOMOD, 0, 0}, {3, F_BITS, 6, 1}, {3, F_BITS, 7, 1}};

static const sq80_param_info OSC_I[SQ80_P_OSC_N] = {
    {"OCT", -3, 5, SQ80_K_SIGNED},  {"SEMI", 0, 11, SQ80_K_NUM},     {"FINE", 0, 31, SQ80_K_NUM},
    {"WAVE", 0, 74, SQ80_K_WAVE},   {"MOD1", 0, 15, SQ80_K_SOURCE},  {"DEPTH1", -63, 63, SQ80_K_SIGNED},
    {"MOD2", 0, 15, SQ80_K_SOURCE}, {"DEPTH2", -63, 63, SQ80_K_SIGNED}, {"LEVEL", 0, 63, SQ80_K_NUM},
    {"OUTPUT", 0, 1, SQ80_K_ONOFF}, {"DMOD1", 0, 15, SQ80_K_SOURCE}, {"DDEPTH1", -63, 63, SQ80_K_SIGNED},
    {"DMOD2", 0, 15, SQ80_K_SOURCE}, {"DDEPTH2", -63, 63, SQ80_K_SIGNED}};
static const place OSC_P[SQ80_P_OSC_N] = {
    {0, F_OCT, 0, 0},  {0, F_SEMI, 0, 0}, {1, F_BITS, 3, 5}, {5, F_BITS, 0, 8}, {2, F_BITS, 0, 4},
    {3, F_HALF, 0, 0}, {2, F_BITS, 4, 4}, {4, F_HALF, 0, 0}, {6, F_BITS, 1, 6}, {6, F_BITS, 7, 1},
    {7, F_BITS, 0, 4}, {8, F_HALF, 0, 0}, {7, F_BITS, 4, 4}, {9, F_HALF, 0, 0}};

static const sq80_param_info PROG_I[SQ80_P_PROG_N] = {
    {"FREQ", 0, 127, SQ80_K_NUM},     {"RES", 0, 31, SQ80_K_NUM},        {"KEYBD", 0, 63, SQ80_K_NUM},
    {"MOD1", 0, 15, SQ80_K_SOURCE},   {"DEPTH1", -63, 63, SQ80_K_SIGNED}, {"MOD2", 0, 15, SQ80_K_SOURCE},
    {"DEPTH2", -63, 63, SQ80_K_SIGNED}, {"FINAL", 0, 63, SQ80_K_NUM},    {"PAN", 0, 15, SQ80_K_NUM},
    {"PANMOD", 0, 15, SQ80_K_SOURCE}, {"PANDPTH", -63, 63, SQ80_K_SIGNED}, {"SYNC", 0, 1, SQ80_K_ONOFF},
    {"AM", 0, 1, SQ80_K_ONOFF},       {"MONO", 0, 1, SQ80_K_ONOFF},      {"GLIDE", 0, 127, SQ80_K_NUM},
    {"VC", 0, 1, SQ80_K_ONOFF},       {"ENV", 0, 1, SQ80_K_ONOFF},       {"OSC", 0, 1, SQ80_K_ONOFF},
    {"CYC", 0, 1, SQ80_K_ONOFF},      {"CHAR 1", 32, 126, SQ80_K_CHAR},  {"CHAR 2", 32, 126, SQ80_K_CHAR},
    {"CHAR 3", 32, 126, SQ80_K_CHAR}, {"CHAR 4", 32, 126, SQ80_K_CHAR},  {"CHAR 5", 32, 126, SQ80_K_CHAR},
    {"CHAR 6", 32, 126, SQ80_K_CHAR},  {"LAYER", 0, 40, SQ80_K_PROG},     {"SPLIT", 0, 40, SQ80_K_PROG},
    {"SPL LYR", 0, 40, SQ80_K_PROG},  {"SPL KEY", 0, 127, SQ80_K_KEY}};
static const place PROG_P[SQ80_P_PROG_N] = {
    {0x59, F_BITS, 0, 7}, {0x5a, F_BITS, 0, 5}, {0x5e, F_BITS, 1, 6}, {0x5b, F_BITS, 0, 4}, {0x5c, F_S7, 0, 0},
    {0x5b, F_BITS, 4, 4}, {0x5d, F_S7, 0, 0},   {0x58, F_BITS, 1, 6}, {0x64, F_BITS, 4, 4}, {0x64, F_BITS, 0, 4},
    {0x65, F_S7, 0, 0},   {0x59, F_BITS, 7, 1}, {0x58, F_BITS, 7, 1}, {0x5d, F_BITS, 7, 1}, {0x5f, F_BITS, 0, 7},
    {0x5c, F_BITS, 7, 1}, {0x5e, F_BITS, 7, 1}, {0x5f, F_BITS, 7, 1}, {0x65, F_BITS, 7, 1}, {0, F_CHAR, 0, 0},
    {1, F_CHAR, 0, 0},    {2, F_CHAR, 0, 0},    {3, F_CHAR, 0, 0},    {4, F_CHAR, 0, 0},    {5, F_CHAR, 0, 0},
    {0x61, F_LINK, 0, 0}, {0x62, F_LINK, 0, 0}, {0x63, F_LINK, 0, 0}, {0x60, F_BITS, 0, 7}};

static int where(int id, place *pl, const sq80_param_info **info)
{
    if (id < 0 || id >= SQ80_PARAMS)
        return 0;
    if (id < SQ80_PARAM_LFO(0, 0)) {
        int n = id / SQ80_P_ENV_N, k = id % SQ80_P_ENV_N;
        *pl = ENV_P[k];
        pl->off = (uint8_t)(pl->off + 6 + 10 * n);
        *info = &ENV_I[k];
    } else if (id < SQ80_PARAM_OSC(0, 0)) {
        int r = id - SQ80_PARAM_LFO(0, 0), n = r / SQ80_P_LFO_N, k = r % SQ80_P_LFO_N;
        *pl = LFO_P[k];
        pl->off = (uint8_t)(pl->off + 0x2e + 4 * n);
        *info = &LFO_I[k];
    } else if (id < SQ80_PARAM_PROG(0)) {
        int r = id - SQ80_PARAM_OSC(0, 0), n = r / SQ80_P_OSC_N, k = r % SQ80_P_OSC_N;
        *pl = OSC_P[k];
        pl->off = (uint8_t)(pl->off + 0x3a + 10 * n);
        *info = &OSC_I[k];
    } else {
        int k = id - SQ80_PARAM_PROG(0);
        *pl = PROG_P[k];
        *info = &PROG_I[k];
    }
    return 1;
}

const sq80_param_info *sq80_param(int id)
{
    place pl;
    const sq80_param_info *info;
    return where(id, &pl, &info) ? info : NULL;
}

static int s8(int b) { return b >= 128 ? b - 256 : b; }

int sq80_param_get(const uint8_t p[102], int id)
{
    place pl;
    const sq80_param_info *info;
    int b, v;
    if (!where(id, &pl, &info))
        return 0;
    b = p[pl.off];
    switch (pl.f) {
    case F_BITS: v = (b >> pl.shift) & ((1 << pl.width) - 1); break;
    case F_HALF: v = s8(b) / 2; break;   /* (C rounds toward zero, as the SQ-80) */
    case F_S7: v = (b & 0x7f) >= 64 ? (b & 0x7f) - 128 : (b & 0x7f); break;
    case F_OCT: v = (b & 0x7f) / 12 - 3; break;
    case F_SEMI: v = (b & 0x7f) % 12; break;
    case F_LFOMOD: v = ((b >> 6) << 2) | (p[pl.off + 1] >> 6); break;
    case F_LINK: v = b & 0x80 ? (b & 0x7f) + 1 : 0; break;   /* (a cartridge's, past 40: shown as 40) */
    default: v = b; break;
    }
    return v < info->min ? info->min : v > info->max ? info->max : v;
}

void sq80_param_set(uint8_t p[102], int id, int value)
{
    place pl;
    const sq80_param_info *info;
    uint8_t *b;
    int v;
    if (!where(id, &pl, &info))
        return;
    v = value < info->min ? info->min : value > info->max ? info->max : value;
    b = &p[pl.off];
    switch (pl.f) {
    case F_BITS: {
        int mask = ((1 << pl.width) - 1) << pl.shift;
        *b = (uint8_t)((*b & ~mask) | ((v << pl.shift) & mask));
        break;
    }
    case F_HALF: *b = (uint8_t)((v * 2) & 0xff); break;
    case F_S7: *b = (uint8_t)((*b & 0x80) | (v & 0x7f)); break;
    case F_OCT: *b = (uint8_t)((*b & 0x80) | ((v + 3) * 12 + (*b & 0x7f) % 12)); break;   /* (at most 107) */
    case F_SEMI: {   /* (the octave as get reads it: clamped to -3..5) */
        int o = (*b & 0x7f) / 12;
        *b = (uint8_t)((*b & 0x80) | ((o > 8 ? 8 : o) * 12 + v));
        break;
    }
    case F_LFOMOD:
        *b = (uint8_t)((*b & 0x3f) | ((v >> 2) << 6));
        b[1] = (uint8_t)((b[1] & 0x3f) | ((v & 3) << 6));
        break;
    case F_LINK: *b = (uint8_t)(v ? 0x80 | (v - 1) : *b & 0x7f); break;
    default: *b = (uint8_t)v; break;
    }
}

void sq80_init_program(uint8_t p[102])
{
    int i, k;
    static const char NAME[] = "INIT  ";
    for (i = 0; i < 102; i++)
        p[i] = 0;
    for (i = 0; i < 6; i++)
        p[i] = (uint8_t)NAME[i];
    for (i = 0; i < 3; i++) {
        sq80_param_set(p, SQ80_PARAM_OSC(i, SQ80_P_OSC_OCT), 0);
        for (k = SQ80_P_OSC_MOD1; k <= SQ80_P_OSC_DMOD2; k += 2)
            if (k != SQ80_P_OSC_LEVEL)
                sq80_param_set(p, SQ80_PARAM_OSC(i, k), 15);
        sq80_param_set(p, SQ80_PARAM_LFO(i, SQ80_P_LFO_MOD), 15);
    }
    sq80_param_set(p, SQ80_PARAM_OSC(0, SQ80_P_OSC_LEVEL), 63);
    sq80_param_set(p, SQ80_PARAM_OSC(0, SQ80_P_OSC_OUTPUT), 1);
    for (k = SQ80_P_ENV_L1; k <= SQ80_P_ENV_L3; k++)
        sq80_param_set(p, SQ80_PARAM_ENV(3, k), 63);
    sq80_param_set(p, SQ80_PARAM_PROG(SQ80_P_FREQ), 127);
    sq80_param_set(p, SQ80_PARAM_PROG(SQ80_P_FMOD1), 15);
    sq80_param_set(p, SQ80_PARAM_PROG(SQ80_P_FMOD2), 15);
    sq80_param_set(p, SQ80_PARAM_PROG(SQ80_P_FINAL), 63);
    sq80_param_set(p, SQ80_PARAM_PROG(SQ80_P_PAN), 8);
    sq80_param_set(p, SQ80_PARAM_PROG(SQ80_P_PANMOD), 15);
    sq80_param_set(p, SQ80_PARAM_PROG(SQ80_P_SPKEY), 60);   /* (the split key at C4; no split, no layer) */
}

const char *sq80_source_name(int s)
{
    static const char *const N[16] = {"LFO1", "LFO2", "LFO3", "ENV1", "ENV2", "ENV3", "ENV4", "VEL",
                                      "VELX", "KBD", "KBD2", "WHEEL", "PEDAL", "XCTRL", "PRESS", "OFF"};
    return N[s & 15];
}

const char *sq80_lfo_wave_name(int w)
{
    static const char *const N[4] = {"TRI", "SAW", "SQR", "NOIS"};
    return N[w & 3];
}
