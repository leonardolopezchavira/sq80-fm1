/* SPDX-License-Identifier: MIT
 * SQ-80 / ESQ-1 engine: portable C99, single-precision float, no allocation, no OS calls.
 * The same source runs in the desktop tools and as M-VAVE FM-1 firmware (fm1-base app). */
#ifndef SQ80_H
#define SQ80_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- ROM pack
 * The wave directory (from the SQ-80 OS ROM) and the 256 KiB wave ROM, built from the user's own
 * dumps by tools/make_rompack.py (docs/rompack.md). Read in place: on the FM-1, from flash. */

#define SQ80_WAVES 75
#define SQ80_ZONES 16
#define SQ80_WAVE_ROM_SIZE (256u * 1024u)   /* 256 KiB */

typedef struct {
    uint32_t start;      /* byte offset into the wave ROM */
    uint8_t size_log2;   /* table length = 1 << size_log2 (256 .. 32768) */
    uint8_t resolution;  /* DOC address resolution 0..7 */
    int8_t tune_semi;    /* signed semitone offset of the sample */
    uint8_t tune_frac;   /* 1/256 semitone */
} sq80_raw_wave;

typedef struct {
    const uint8_t *names;  /* 75 x 6 characters */
    const uint8_t *zones;  /* 75 x 16 raw-wave indices */
    const uint8_t *raws;   /* raw count x 4 */
    const uint8_t *rom;    /* the wave ROM: 256 KiB for the SQ-80; a test pack may be smaller */
    uint32_t rom_mask;     /* rom size - 1 (a power of two) */
    int raw_count;
} sq80_rompack;

/* 1 if `data` is a valid version-1 ROM pack (pointers into `data` are kept), else 0. */
int sq80_rompack_load(sq80_rompack *pack, const uint8_t *data, size_t size);
sq80_raw_wave sq80_rompack_raw(const sq80_rompack *pack, int index);
/* wave name, 6 characters + NUL */
void sq80_rompack_name(const sq80_rompack *pack, int wave, char out[7]);

/* A test pack with synthetic waves (no Ensoniq data), built into `buf` (SQ80_TEST_PACK_SIZE bytes):
 * SAW (wave 0) a saw, SINE (2) a sine, SQUARE (3) and PULSE (4) a square, the one-shot waves (54..74) a
 * decaying tone that stops, every other wave the sine. Returns the pack's size. */
#define SQ80_TEST_PACK_SIZE (32u + 1680u + 8192u)   /* header, directory (padded to 16), ROM */
size_t sq80_test_pack(uint8_t *buf);

/* ------------------------------------------------------------------------- programs */

enum { /* modulation sources, SQ-80 numbering */
    SQ80_LFO1, SQ80_LFO2, SQ80_LFO3, SQ80_ENV1, SQ80_ENV2, SQ80_ENV3, SQ80_ENV4, SQ80_VEL, SQ80_VELX,
    SQ80_KBD, SQ80_KBD2, SQ80_WHEEL, SQ80_PEDAL, SQ80_XCTRL, SQ80_PRESS, SQ80_OFF
};

typedef struct { uint8_t source; int8_t depth; } sq80_mod;   /* depth -63..63 */

typedef struct {
    int8_t l1, l2, l3;              /* -63..63 */
    uint8_t t1, t2, t3, t4;         /* 0..63 */
    uint8_t second_release;         /* T4 "R" */
    uint8_t lv, t1v, tk;            /* 0..63 */
} sq80_env_params;

typedef struct {
    uint8_t freq, wave;             /* 0..63; 0 TRI 1 SAW 2 SQR 3 NOISE */
    uint8_t l1, l2, delay;          /* 0..63 */
    uint8_t mod;                    /* source */
    uint8_t humanize, reset;
} sq80_lfo_params;

typedef struct {
    int8_t octave;                  /* -3..5 */
    uint8_t semi, fine, wave;       /* 0..11, 0..31 (1/32 semitone), 0..74 */
    sq80_mod pitch_mod[2];
    uint8_t level, output;          /* DCA 0..63, on */
    sq80_mod level_mod[2];
} sq80_osc_params;

typedef struct {
    char name[7];
    sq80_env_params env[4];
    sq80_lfo_params lfo[3];
    sq80_osc_params osc[3];
    uint8_t final_level;            /* DCA4 0..63 */
    uint8_t am, sync;
    uint8_t cutoff, resonance;      /* 0..127, 0..31 */
    sq80_mod filter_mod[2];
    uint8_t key_track;              /* 0..63 */
    uint8_t restart_voice, mono, restart_env, restart_osc, cycle;
    uint8_t glide;                  /* 0..127 */
    uint8_t pan;                    /* 0..15 */
    sq80_mod pan_mod;
} sq80_patch;

/* Decodes a 102-byte ESQ-1/SQ-80 program (docs/patch-format.md). */
void sq80_decode_patch(const uint8_t program[102], sq80_patch *out);
/* A neutral patch: OSC1 SAW at full level, filter open, ENV4 held. */
void sq80_default_patch(sq80_patch *out);

/* Reads the programs of a SysEx stream: single (F0 0F 02 ch 01) and bank (F0 0F 02 ch 02) dumps. */
typedef struct { const uint8_t *data; size_t size, pos; int left; } sq80_sysex_reader;
void sq80_sysex_begin(sq80_sysex_reader *r, const uint8_t *data, size_t size);
int sq80_sysex_next(sq80_sysex_reader *r, uint8_t out[102]);   /* 1 = a program, 0 = end */

/* ------------------------------------------------------------------------- the synth */

#define SQ80_VOICES 8

typedef struct {
    sq80_env_params p;
    int levels[4], scale, tk_offset, t1_offset;
    int value, target, step, ticks, output;
    uint8_t stage, cycle, released, active;
} sq80_env;

typedef struct {
    sq80_lfo_params p;
    uint32_t phase, noise;
    int level, level_target, emu_clock, output, noise_held, human_offset;
} sq80_lfo;

typedef struct {
    uint32_t phase, inc, start;
    uint8_t shift, halted, one_shot, wrapped;
    int raw_index;
    float amp, amp_target;
} sq80_osc;

typedef struct { float g, k, s[4]; } sq80_filter;

typedef struct {
    uint8_t active, released;
    int key, velocity;
    uint32_t age;
    float pitch_key, glide_rate;
    sq80_osc osc[3];
    sq80_env env[4];
    sq80_lfo lfo[3];
    sq80_filter filter;
    float hist[4], doc_phase;
    float gain_l, gain_r, gain_l_target, gain_r_target;
    float cutoff, cutoff_target, resonance;
} sq80_voice;

typedef struct {
    const sq80_rompack *rom;
    sq80_patch patch;
    sq80_voice voices[SQ80_VOICES];
    float sample_rate, doc_step, samples_per_tick, control_countdown, amp_pole;
    uint32_t age_counter;
    int held_keys[16], held_count, last_key;
    int wheel, pedal, xctrl, pressure, volume, sustain;
    float bend_semis;
    uint8_t lfo_started[3];
    uint32_t filter_counter;
} sq80_synth;

/* `rom` must stay valid (on the FM-1 it lives in flash). sample_rate >= 32000. */
void sq80_init(sq80_synth *s, const sq80_rompack *rom, float sample_rate);
void sq80_set_patch(sq80_synth *s, const sq80_patch *p);
void sq80_note_on(sq80_synth *s, int key, int velocity);
void sq80_note_off(sq80_synth *s, int key);
void sq80_control_change(sq80_synth *s, int cc, int value);
void sq80_pitch_bend(sq80_synth *s, int value14);       /* 0..16383, 8192 = centre */
void sq80_channel_pressure(sq80_synth *s, int value);
void sq80_all_notes_off(sq80_synth *s);
/* Renders n stereo frames into left/right (overwritten). */
void sq80_render(sq80_synth *s, float *left, float *right, int n);
int sq80_active_voices(const sq80_synth *s);

#ifdef __cplusplus
}
#endif
#endif
