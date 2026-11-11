/* SPDX-License-Identifier: MIT
 * Every value of an ESQ-1 / SQ-80 program (102 bytes, docs/patch-format.md), read and written in place: an editor
 * changes the bytes it means to and no others, so a program that is only played and saved again comes back byte
 * for byte. sq80_decode_patch turns the bytes into what the engine plays. */
#ifndef SQ80_PARAMS_H
#define SQ80_PARAMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* what a value is, for showing it */
enum { SQ80_K_NUM, SQ80_K_SIGNED, SQ80_K_ONOFF, SQ80_K_SOURCE, SQ80_K_WAVE, SQ80_K_LFOWAVE, SQ80_K_CHAR,
       SQ80_K_PROG,     /* 0 OFF, 1..40 a program of the bank (its place) */
       SQ80_K_KEY };    /* a key, 0..127 (60 = C4) */

typedef struct {
    const char *name;   /* up to 7 characters, the SQ-80's own words where it has them */
    int16_t min, max;
    uint8_t kind;       /* SQ80_K_* */
} sq80_param_info;

/* the parameters, by group: ENV n (11 each), LFO n (8), OSC n (14), then the program's own and its name */
enum {
    SQ80_P_ENV_L1, SQ80_P_ENV_L2, SQ80_P_ENV_L3, SQ80_P_ENV_LV, SQ80_P_ENV_T1, SQ80_P_ENV_T2, SQ80_P_ENV_T3,
    SQ80_P_ENV_T4, SQ80_P_ENV_T1V, SQ80_P_ENV_TK, SQ80_P_ENV_R2, SQ80_P_ENV_N
};
enum {
    SQ80_P_LFO_FREQ, SQ80_P_LFO_WAVE, SQ80_P_LFO_L1, SQ80_P_LFO_L2, SQ80_P_LFO_DELAY, SQ80_P_LFO_MOD,
    SQ80_P_LFO_HUMAN, SQ80_P_LFO_RESET, SQ80_P_LFO_N
};
enum {
    SQ80_P_OSC_OCT, SQ80_P_OSC_SEMI, SQ80_P_OSC_FINE, SQ80_P_OSC_WAVE, SQ80_P_OSC_MOD1, SQ80_P_OSC_DEPTH1,
    SQ80_P_OSC_MOD2, SQ80_P_OSC_DEPTH2, SQ80_P_OSC_LEVEL, SQ80_P_OSC_OUTPUT, SQ80_P_OSC_DMOD1, SQ80_P_OSC_DDEPTH1,
    SQ80_P_OSC_DMOD2, SQ80_P_OSC_DDEPTH2, SQ80_P_OSC_N
};
enum {
    SQ80_P_FREQ, SQ80_P_RES, SQ80_P_KEYBD, SQ80_P_FMOD1, SQ80_P_FDEPTH1, SQ80_P_FMOD2, SQ80_P_FDEPTH2,
    SQ80_P_FINAL, SQ80_P_PAN, SQ80_P_PANMOD, SQ80_P_PANDEPTH, SQ80_P_SYNC, SQ80_P_AM, SQ80_P_MONO, SQ80_P_GLIDE,
    SQ80_P_VC, SQ80_P_ENVRS, SQ80_P_OSCRS, SQ80_P_CYC, SQ80_P_NAME0,
    SQ80_P_LAYER = SQ80_P_NAME0 + 6, SQ80_P_SPLIT, SQ80_P_SPLAYR, SQ80_P_SPKEY,   /* SPLIT/LAYER (after the name: the
                                                                               * ids before keep their numbers) */
    SQ80_P_PROG_N
};
#define SQ80_PARAM_ENV(n, p) ((n) * SQ80_P_ENV_N + (p))
#define SQ80_PARAM_LFO(n, p) (4 * SQ80_P_ENV_N + (n) * SQ80_P_LFO_N + (p))
#define SQ80_PARAM_OSC(n, p) (4 * SQ80_P_ENV_N + 3 * SQ80_P_LFO_N + (n) * SQ80_P_OSC_N + (p))
#define SQ80_PARAM_PROG(p) (4 * SQ80_P_ENV_N + 3 * SQ80_P_LFO_N + 3 * SQ80_P_OSC_N + (p))
#define SQ80_PARAMS SQ80_PARAM_PROG(SQ80_P_PROG_N)

const sq80_param_info *sq80_param(int id);              /* 0 for an id out of range */
int sq80_param_get(const uint8_t p[102], int id);
void sq80_param_set(uint8_t p[102], int id, int value); /* clamped to the parameter's range */

/* a plain program to start from: OSC1 a saw, the filter open, ENV4 held, every modulation off, named INIT */
void sq80_init_program(uint8_t p[102]);

/* the modulation sources' and the LFO waves' names */
const char *sq80_source_name(int s);                    /* "LFO1" .. "PRESS", "OFF" */
const char *sq80_lfo_wave_name(int w);                  /* "TRI", "SAW", "SQR", "NOIS" */

#ifdef __cplusplus
}
#endif
#endif
