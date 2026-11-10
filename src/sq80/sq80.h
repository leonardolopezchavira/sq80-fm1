/* SPDX-License-Identifier: MIT
 * SQ-80 / ESQ-1 engine: portable C99, single-precision float, no allocation, no OS calls.
 * The same source runs in the desktop tools and as M-VAVE FM-1 firmware (fm1-base app). */
#ifndef SQ80_H
#define SQ80_H

#include <stddef.h>
#include <stdint.h>

/* a ROM pack kept in pieces (the SMK-37: no one stretch of its flash holds it), else one block */
#ifndef SQ80_ROM_PAGED
#if defined(BOARD_SMK37) && BOARD_SMK37
#define SQ80_ROM_PAGED 1
#else
#define SQ80_ROM_PAGED 0
#endif
#endif

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
    const uint8_t *rom;    /* the wave ROM: 256 KiB for the SQ-80; a test pack may be smaller (0: paged) */
    uint32_t rom_mask;     /* rom size - 1 (a power of two) */
    int raw_count;
#if SQ80_ROM_PAGED
    const uint8_t *const *sec;   /* a pack kept in 4 KiB pieces: sector i of the pack at sec[i] (0: in one piece) */
    uint32_t rom_off;      /* the wave ROM's offset in the pack */
#endif
} sq80_rompack;

/* byte a of the wave ROM (a masked to its size) */
static inline uint8_t sq80_rom_at(const sq80_rompack *p, uint32_t a) {
    a &= p->rom_mask;
#if SQ80_ROM_PAGED
    if (p->sec) {
        a += p->rom_off;
        return p->sec[a >> 12][a & 4095u];
    }
#endif
    return p->rom[a];
}

/* 1 if `data` is a valid version-1 ROM pack (pointers into `data` are kept), else 0. */
int sq80_rompack_load(sq80_rompack *pack, const uint8_t *data, size_t size);
#if SQ80_ROM_PAGED
/* the same for a pack kept in n 4 KiB pieces (sec[i]: its sector i; the array is kept): its directory must lie in the
 * first */
int sq80_rompack_load_paged(sq80_rompack *pack, const uint8_t *const *sec, size_t n);
#endif
sq80_raw_wave sq80_rompack_raw(const sq80_rompack *pack, int index);
/* wave name, 6 characters + NUL */
void sq80_rompack_name(const sq80_rompack *pack, int wave, char out[7]);

/* A test pack with synthetic waves (no Ensoniq data), built into `buf` (SQ80_TEST_PACK_SIZE bytes):
 * SAW (wave 0) a saw, SINE (2) a sine, SQUARE (3) and PULSE (4) a square, the one-shot waves (54..74) a
 * decaying tone that stops, every other wave the sine. Returns the pack's size. */
#define SQ80_TEST_PACK_SIZE (32u + 1680u + 8192u)   /* header, directory (padded to 16), ROM */
size_t sq80_test_pack(uint8_t *buf);
/* bytes off .. off+n-1 of the same, for building it a piece at a time */
void sq80_test_pack_part(uint8_t *buf, size_t off, size_t n);

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

typedef struct { float k, G, G2, G3, G4, inv, den, s[4]; } sq80_filter;   /* coefficients from filter_set */

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
/* The same in parts, for two cores: sq80_render_part for disjoint voice ranges (each into its own
 * buffers, cleared first; they may run at once), the buffers summed, then sq80_render_commit(s, n) once. */
void sq80_render_part(sq80_synth *s, float *left, float *right, int n, int first, int count);
void sq80_render_commit(sq80_synth *s, int n);
int sq80_active_voices(const sq80_synth *s);

/* For a picture of the filter (the FM-1's FILTER page, the editor): the cutoff in Hz that FREQ (0..127) and KEYBD
 * (0..63) give a key, as a voice sets it (no modulation); and the 4-pole low-pass's response at hz, in dB, for that
 * cutoff and RES (0..31): the analog ladder the engine models, with its passband compensation */
float sq80_filter_cutoff(int freq, int keybd, int key);
float sq80_filter_db(float hz, float cutoff, int res);
/* the response from 20 Hz to 20 kHz in n columns (log spaced), in whole dB (-120..60), for FREQ, KEYBD, RES and a key */
void sq80_filter_curve(int freq, int keybd, int res, int key, signed char *db, int n);

#ifdef __cplusplus
}
#endif
#endif
