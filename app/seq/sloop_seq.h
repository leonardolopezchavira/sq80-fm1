/* SPDX-License-Identifier: GPL-3.0-only
 * SQ-80 for the FM-1: SLOOP's arpeggiator, step sequencer and live recording, for one synth part.
 * Ported from SLOOP (firmware/src/seq.c, core.h), Copyright (C) 2026 Leo Kuroshita (@kurogedelic),
 * Hügelton Instruments, GPL-3.0-only: https://github.com/isod89/sloop-fm1
 * Changes for the SQ-80 Copyright (C) 2026 Leonardo López Chavira. */
#pragma once
#include <stdint.h>

#define SL_NSTEP 64u
enum { SL_ST_NOTE, SL_ST_TIE, SL_ST_REST };   /* a step's time (SLOOP's ST_*) */
#define SL_SF_ACCENT 1u
#define SL_SF_SLIDE 2u
/* a step, as SLOOP's step_t: up to 4 notes, each with a level (2 bits: 0 its velocity, 1..3 ghost / soft / hard) and
 * a ratchet (2 bits: 1..4 hits in the step) */
typedef struct {
    uint8_t note[4];
    uint8_t n;
    uint8_t time;
    uint8_t flags;
    uint8_t vel;
    uint8_t lvl;
    uint8_t rat;
} sl_step;

/* the settings (sl_song.p) */
enum {
    SL_AON,      /* the arpeggiator: off / on */
    SL_AMODE,    /* 1..14: UP DOWN UP/DN RANDOM ORDER REPEAT DN/UP UP+8 CONV DIVG PINKY THUMB WALK CHORD */
    SL_ARATE,    /* 0..5: 1/4 1/8 1/16 1/32 1/8T 1/16T */
    SL_AOCT,     /* 1..4 */
    SL_AGATE,    /* 1..127 (/128 of a step) */
    SL_ASWING,   /* 0..100 */
    SL_AHOLD,    /* off / on: the notes go on when let go */
    SL_AORDER,   /* 0 the notes low to high, 1 as played */
    SL_APROB,    /* 0..127: the chance a step plays */
    SL_SLEN,     /* 1..64 steps */
    SL_SDIV,     /* 0..8: 1/4 1/8 1/16 1/32 1/8T 1/16T 1/2 1 BAR 2 BARS */
    SL_SGATE,    /* 1..127 */
    SL_SSWING,   /* 0..100 */
    SL_BPM,      /* 40..240 */
    SL_SYNC,     /* off / on: follow a MIDI clock that comes in */
    SL_RSV,
    SL_NP
};
typedef struct {
    uint8_t p[SL_NP];
    sl_step step[SL_NSTEP];
} sl_song;

typedef struct {
    const char *name;
    uint8_t min, max, def;
    const char *const *names;            /* the values' names from min (0: a number) */
} sl_param_info;
const sl_param_info *sl_param(uint32_t id);
void sl_defaults(sl_song *s);
void sl_clamp(sl_song *s);
const char *sl_value_text(uint32_t id, int32_t v, char *t);

/* the hooks (the app's): a note of the sequencer, the arpeggiator or the player's keys starts / ends */
void sl_out_on(uint32_t note, uint32_t vel);
void sl_out_off(uint32_t note);

/* all of the following run in the audio interrupt (the app posts its UI's commands there) */
void sl_init(sl_song *s);
void sl_key(uint32_t note, uint32_t vel);   /* a key or a MIDI note: vel 0 = up */
void sl_realtime(uint32_t st, uint32_t ms);   /* MIDI clock, start, continue, stop */
void sl_block(uint32_t n, uint32_t ms);       /* a block of n samples begins */
uint32_t sl_next(void);                        /* samples to the next event in this block (large: none) */
void sl_run(uint32_t m);                       /* m samples went by: what falls due happens */
void sl_all_off(void);                         /* every note of the sequencer and the arpeggiator ends */

enum {
    SC_PLAY,     /* start / stop */
    SC_REC,      /* playing: record / stop recording; stopped: arm (the first note starts) */
    SC_SET,      /* step a: set to the last notes played */
    SC_CLEAR,    /* step a: rest */
    SC_NOTE,     /* step a: its notes d semitones */
    SC_VEL,      /* step a: its velocity */
    SC_RAT,      /* step a: its ratchets */
    SC_TYPE,     /* step a: NOTE, ACCENT, SLIDE, TIE */
    SC_MARK,     /* an edit begins (undo keeps the pattern before it) */
    SC_UNDO,
    SC_REDO,
    SC_WIPE,     /* the pattern cleared */
    SC_STOP
};
void sl_cmd(uint32_t cmd, uint32_t a, int32_t d);

/* the state, for the screen and the lights (main loop: read only) */
enum { SL_S_PLAY = 1, SL_S_REC = 2, SL_S_ARMED = 4, SL_S_BEAT = 8, SL_S_SYNC = 16, SL_S_UNDO = 32, SL_S_REDO = 64 };
uint32_t sl_state(void);
int32_t sl_pos(void);                          /* the step playing, -1 stopped */
uint32_t sl_changes(void);                     /* the pattern's changes so far (a count) */
