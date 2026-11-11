/* SPDX-License-Identifier: MIT
 * SQ-80's engine for the editor page (app/editor/index.html): the same C as the FM-1's (src/sq80), in WebAssembly,
 * with no C library. The page writes a program into prog(), a ROM pack into pack_buf() (or keeps the built-in waves),
 * plays notes and pulls audio with render(); it reads and writes every value through param_* (sq80_params). Built by
 * tools/build_editor.sh (emcc, or clang --target=wasm32). */
#include "sq80.h"
#include "sq80_params.h"
#include "sq80_waves.h"
#include "sq80_random.h"                   /* (Randomize: the FM-1's HOME RANDOM) */
#include "sq80_cal.h"                      /* (SQ80_ENV_TICKS: the envelope drawing) */

#define EXPORT __attribute__((used, visibility("default")))
#define MAXN 4096
#define PACK_MAX (300u * 1024u)

static sq80_rompack rom;
static sq80_synth syn;
static sq80_patch patch;
static uint8_t program[102];
static uint8_t pack[PACK_MAX];
static int user_pack, started;
static float out_l[MAXN], out_r[MAXN];
static char text[16];

/* (the compiler may call these for struct copies) */
void *memset(void *d, int c, unsigned long n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, unsigned long n)
{
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--)
        *p++ = *q++;
    return d;
}

EXPORT int init(int sample_rate)
{
    user_pack = 0;
    if (!sq80_rompack_load(&rom, SQ80_WAVES_PACK, SQ80_WAVES_SIZE))
        return 0;
    sq80_init(&syn, &rom, (float)sample_rate);
    if (!started)                            /* (again at the audio's own rate: the program stays) */
        sq80_init_program(program);
    started = 1;
    sq80_decode_patch(program, &patch);
    sq80_set_patch(&syn, &patch);
    return 1;
}

EXPORT uint8_t *prog(void) { return program; }
EXPORT uint8_t *pack_buf(void) { return pack; }
EXPORT int pack_max(void) { return (int)PACK_MAX; }

/* a ROM pack the page put in pack_buf (n bytes): 1 taken; n = 0: the built-in waves again */
EXPORT int use_pack(int n)
{
    sq80_all_notes_off(&syn);
    if (n > 0 && sq80_rompack_load(&rom, pack, (size_t)n)) {
        user_pack = 1;
    } else {
        user_pack = 0;
        sq80_rompack_load(&rom, SQ80_WAVES_PACK, SQ80_WAVES_SIZE);
        if (n > 0)
            return 0;
    }
    syn.rom = &rom;
    return 1;
}
EXPORT int has_user_pack(void) { return user_pack; }

/* the SPLIT/LAYER partners (layer, split, split layer) as the page has them from the open bank: their programs in
 * link_buf(), which of them are there in link_mask_set's bits; used by the next apply */
static uint8_t links_raw[3][102];
static sq80_patch links[3];
static int link_mask;
EXPORT uint8_t *link_buf(void) { return &links_raw[0][0]; }
EXPORT void link_mask_set(int m) { link_mask = m; }

/* the program in prog() into the engine: fresh 1 cuts the notes (a new program), 0 lets them go on (an edit) */
EXPORT void apply(int fresh)
{
    int j;
    sq80_decode_patch(program, &patch);
    if (fresh)
        sq80_all_notes_off(&syn);
    sq80_set_patch(&syn, &patch);
    for (j = 0; j < 3; j++)
        if (link_mask >> j & 1)
            sq80_decode_patch(links_raw[j], &links[j]);
    sq80_set_links(&syn, link_mask & 1 ? &links[0] : 0, link_mask & 2 ? &links[1] : 0, link_mask & 4 ? &links[2] : 0,
                   patch.split_key);
}

EXPORT void note_on(int key, int vel) { sq80_note_on(&syn, key, vel); }
EXPORT void note_off(int key) { sq80_note_off(&syn, key); }
EXPORT void cc(int c, int v) { sq80_control_change(&syn, c, v); }
EXPORT void bend(int v) { sq80_pitch_bend(&syn, v); }
EXPORT void all_off(void) { sq80_all_notes_off(&syn); }
EXPORT int voices(void) { return sq80_active_voices(&syn); }

EXPORT float *left(void) { return out_l; }
EXPORT float *right(void) { return out_r; }
EXPORT void render(int n)
{
    int i;
    if (n > MAXN)
        n = MAXN;
    for (i = 0; i < n; i++)
        out_l[i] = out_r[i] = 0.0f;
    sq80_render(&syn, out_l, out_r, n);
}

/* the values: SQ80_PARAMS of them (sq80_params.h order) */
EXPORT int param_count(void) { return SQ80_PARAMS; }
EXPORT const char *param_name(int id)
{
    const sq80_param_info *in = sq80_param(id);
    return in ? in->name : "";
}
EXPORT int param_min(int id) { const sq80_param_info *in = sq80_param(id); return in ? in->min : 0; }
EXPORT int param_max(int id) { const sq80_param_info *in = sq80_param(id); return in ? in->max : 0; }
EXPORT int param_kind(int id) { const sq80_param_info *in = sq80_param(id); return in ? in->kind : 0; }
EXPORT int param_get(int id) { return sq80_param_get(program, id); }
EXPORT void param_set(int id, int v) { sq80_param_set(program, id, v); }
EXPORT void init_program(void) { sq80_init_program(program); }
/* the filter's picture: the cutoff (Hz) for a key, and the response (dB) at hz (sq80_filter_*) */
EXPORT float filter_cutoff(int key)
{
    return sq80_filter_cutoff(sq80_param_get(program, SQ80_PARAM_PROG(SQ80_P_FREQ)),
                              sq80_param_get(program, SQ80_PARAM_PROG(SQ80_P_KEYBD)), key);
}
EXPORT float filter_db(float hz, float cutoff) { return sq80_filter_db(hz, cutoff, sq80_param_get(program, SQ80_PARAM_PROG(SQ80_P_RES))); }
/* a new program at random from this one: AMOUNT 5..100, PART 0 all, 1 osc, 2 filter, 3 env, 4 lfo, 5 mod; SEED from
 * the page (a new series each time), named RND + SEQ */
EXPORT void randomize(int amt, int part, unsigned seed, int seq)
{
    uint32_t s = seed ? seed : 1u;
    sq80_randomize(program, amt < 5 ? 5 : amt > 100 ? 100 : amt, (uint32_t)part, (uint32_t)seq, &s);
}

EXPORT const char *wave_name(int w)
{
    sq80_rompack_name(&rom, w, text);
    return text;
}
EXPORT const char *source_name(int s) { return sq80_source_name(s); }
EXPORT const char *lfo_wave_name(int w) { return sq80_lfo_wave_name(w); }

/* the samples wave w plays at key (its zone's raw wave), into out_l as -1..1, up to its end or a 0 byte: their
 * count (for the page's drawing) */
EXPORT int wave_samples(int w, int key)
{
    int raw, i, n;
    sq80_raw_wave r;
    if (w < 0 || w >= SQ80_WAVES)
        return 0;
    if (key < 0)
        key = 0;
    if (key > 127)
        key = 127;
    raw = rom.zones[w * SQ80_ZONES + (key >> 3)];
    r = sq80_rompack_raw(&rom, raw);
    n = 1 << r.size_log2;
    if (n > MAXN)
        n = MAXN;
    for (i = 0; i < n; i++) {
        uint8_t b = rom.rom[(r.start + (uint32_t)i) & rom.rom_mask];
        if (!b)
            break;
        out_l[i] = (float)((int)b - 128) * (1.0f / 127.0f);
    }
    return i;
}

/* an envelope time value 0..63 in control ticks (83.59 a second) */
EXPORT int env_ticks(int t) { return SQ80_ENV_TICKS[t & 63]; }
