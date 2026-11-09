/* SPDX-License-Identifier: GPL-3.0-only
 * SQ-80's effects: FMini-1's (github.com/ruppert/fmini-1, after Felucca by Leo Kuroshita, Hügelton Instruments), the
 * same code: DRIVE (SOFT, TUBE) on the synth, then sends into a CHORUS (STEREO, JUNO, MULTI), a DELAY (ANALOG,
 * DIGITAL, TAPE, P-PONG, MULTI, SLAP, REVERSE) and a REVERB (ROOM, HALL, PLATE, SPRING, CLOUD), and FMini-1's
 * master (DC blocker, limiter, soft clip). app/fx/fmini_fx.c. */
#pragma once
#include <stdint.h>

/* the settings, each 0..127 (the types: their index; FDBK 0..120), FMini-1's own names and defaults */
enum {
    FXP_REV,      /* REVERB  MIX (the send) */
    FXP_RTYPE,    /*         TYPE: ROOM HALL PLATE SPRING CLOUD */
    FXP_RSIZE,    /*         DECAY */
    FXP_RTONE,    /*         TONE (127 open) */
    FXP_DLY,      /* DELAY   MIX */
    FXP_DTYPE,    /*         TYPE: ANALOG DIGITAL TAPE P-PONG MULTI SLAP REVERSE */
    FXP_DTIME,    /*         TIME (10 .. 1400 ms) */
    FXP_DFDBK,    /*         FEEDBK (0..120) */
    FXP_CHO,      /* CHORUS  MIX */
    FXP_CTYPE,    /*         TYPE: STEREO JUNO MULTI */
    FXP_CDEPTH,   /*         DEPTH */
    FXP_CRATE,    /*         RATE */
    FXP_DRIVE,    /* DRIVE   GAIN */
    FXP_DRTYPE,   /*         TYPE: SOFT TUBE */
    FXP_DRTONE,   /*         TONE */
    FXP_DROUT,    /*         OUTPUT */
    FXP_N
};
typedef struct {
    const char *name;
    uint8_t max;
    const char *const *names;   /* the types' names, or 0 */
    uint8_t def;
} fx_param_info;

const fx_param_info *fx_param(int id);
void fx_defaults(uint8_t v[FXP_N]);
const char *fx_value_text(int id, int v, char *buf);   /* "HALL", "650 ms", "64" */

void fx_init(void);
void fx_set(const uint8_t v[FXP_N]);   /* main loop: the settings the next block takes */
/* audio: the synth's block (float, the engine's level) through the effects into lr (24-bit, interleaved);
 * master_q12 the MASTER knob's level; n a multiple of 32 */
void fx_render(const float *l, const float *r, int32_t *lr, uint32_t n, uint32_t master_q12);
