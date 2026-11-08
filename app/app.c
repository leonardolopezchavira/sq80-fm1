/* SPDX-License-Identifier: MIT
 * SQ-80 for the M-VAVE FM-1 (an fm1-base app; started from fm1-base's template, MPL-2.0).
 *
 * The sq80 engine plays the player's own SQ-80 waves and programs, kept in the FM-1's flash:
 *   - the ROM pack (tools/make_rompack.py, from the player's SQ-80 OS and wave ROM dumps) at ROM_LO;
 *   - four banks of 40 programs (ESQ-1 / SQ-80 format, 102 bytes each), one flash sector each, at BANK_LO.
 * Both come in over USB SysEx from the loader page (web/load/index.html) in this app's own framing (below); a
 * single-program ESQ-1/SQ-80 dump (F0 0F 02 ch 01 ...) from any librarian plays at once, unsaved. Without a ROM
 * pack the engine plays a small synthetic wave set (sq80_test_pack: no Ensoniq data), written once to TEST_LO, and
 * bank T holds four test sounds.
 *
 * The panel: PRESET picks the program, SELECT the bank, K1..K4 change the playing program's cutoff, resonance,
 * attack and release (HOME puts them back), OCT- / OCT+ the octave, SAVE keeps bank, program and octave, ARP holds
 * all eight voices (a load test). The second core renders voices 4..7 (fm1.toml cpu2).
 *
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in
 * scope. The engine is compiled apart at -O2 (fm1.toml o2). */
#include "sq80.h"

/* ------------------------------------------------------------- flash map --- */
#define SECTOR 4096u
#define ROM_LO 0x97000u                  /* the ROM pack: 65 sectors, to BANK_LO */
#define ROM_MAX (BANK_LO - ROM_LO)
#define BANK_LO 0xD8000u                 /* four banks, a sector each, to the store's objects at 0xDC000 */
#define NBANK 4u
#define TEST_LO PLAT_XDATA1_LO           /* the synthetic test pack, 3 sectors (its bytes 0xF00..0xF07 never "AT") */
#define BANK_MAGIC 0x4B425153u           /* "SQBK" */
#define NPROG 40u

/* --------------------------------------------------------------- settings --- */
#define SET_MAGIC 0x53513830u            /* "SQ80" */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t bank;                        /* 0: T (the test sounds), 1..4: the flash banks */
    uint8_t prog;                        /* 0 .. 39 */
    uint8_t rsv;
} settings_t;
static settings_t set;
static uint8_t set_dirty;

/* ----------------------------------------------------------------- engine --- */
static sq80_rompack rom;
static uint8_t rom_real;                 /* the player's ROM pack (else the test waves) */
static volatile uint8_t engine_on;       /* 0 while the ROM pack is rewritten: the render keeps quiet */
static sq80_synth syn;
static float buf_l[APP_BLOCK], buf_r[APP_BLOCK];
static float buf2_l[APP_BLOCK], buf2_r[APP_BLOCK];   /* the second core's voices */
static uint8_t two_cores;
static float gain_now;

#define NTEST 4u
static const char *const TEST_NAME[NTEST] = {"SAWPAD", "SQBASS", "SINLED", "ONESHT"};
static sq80_patch base_patch;            /* the program as stored */
static sq80_patch play_patch[2];         /* with the knobs' changes: two, the main loop writes the one not playing */
static uint8_t play_ix;
static char prog_name[8];
static int8_t knob[4];                   /* cutoff, resonance, attack, release: offsets from the program */
static uint8_t edited;

static void test_patch(uint32_t i, sq80_patch *p)
{
    uint32_t k;
    sq80_default_patch(p);
    switch (i) {
    case 0:   /* SAW PAD: two saws a little apart, the filter opened by ENV2, slow attack and release */
        p->osc[1].wave = 0; p->osc[1].fine = 6; p->osc[1].level = 60; p->osc[1].output = 1;
        p->cutoff = 60; p->resonance = 6; p->key_track = 32;
        p->env[1].l1 = 63; p->env[1].l2 = 40; p->env[1].l3 = 30;
        p->env[1].t1 = 40; p->env[1].t2 = 40; p->env[1].t3 = 40; p->env[1].t4 = 35;
        p->filter_mod[0].source = SQ80_ENV2; p->filter_mod[0].depth = 30;
        p->env[3].l1 = 63; p->env[3].l2 = 63; p->env[3].l3 = 63;
        p->env[3].t1 = 30; p->env[3].t2 = 20; p->env[3].t3 = 20; p->env[3].t4 = 38; p->env[3].lv = 20;
        p->lfo[0].freq = 18; p->lfo[0].l1 = 8; p->lfo[0].l2 = 8;
        p->osc[0].pitch_mod[0].source = SQ80_LFO1; p->osc[0].pitch_mod[0].depth = 3;
        p->pan = 6;
        break;
    case 1:   /* SQR BASS: a square an octave down, a resonant filter plucked by ENV2 */
        p->osc[0].wave = 3; p->osc[0].octave = -1;
        p->osc[1].wave = 0; p->osc[1].octave = -1; p->osc[1].level = 50; p->osc[1].output = 1;
        p->cutoff = 30; p->resonance = 20; p->key_track = 40;
        p->env[1].l1 = 63; p->env[1].l2 = 10; p->env[1].t2 = 25; p->env[1].t3 = 30; p->env[1].t4 = 20;
        p->env[1].lv = 30;
        p->filter_mod[0].source = SQ80_ENV2; p->filter_mod[0].depth = 55;
        p->env[3].l1 = 63; p->env[3].l2 = 55; p->env[3].l3 = 50;
        p->env[3].t2 = 30; p->env[3].t3 = 30; p->env[3].t4 = 15; p->env[3].lv = 25;
        break;
    case 2:   /* SINE LEAD: a sine with a fifth above, delayed vibrato, glide */
        p->osc[0].wave = 2;
        p->osc[1].wave = 2; p->osc[1].semi = 7; p->osc[1].level = 40; p->osc[1].output = 1;
        p->osc[2].wave = 0; p->osc[2].octave = 1; p->osc[2].level = 24; p->osc[2].output = 1;
        p->cutoff = 90; p->resonance = 4;
        p->lfo[0].freq = 22; p->lfo[0].l2 = 20; p->lfo[0].delay = 30; p->lfo[0].mod = SQ80_WHEEL;
        for (k = 0; k < 3; k++) {
            p->osc[k].pitch_mod[0].source = SQ80_LFO1;
            p->osc[k].pitch_mod[0].depth = 6;
        }
        p->env[3].l1 = 63; p->env[3].l2 = 63; p->env[3].l3 = 63;
        p->env[3].t1 = 5; p->env[3].t2 = 10; p->env[3].t3 = 10; p->env[3].t4 = 25;
        p->glide = 20;
        break;
    default:  /* ONE-SHOT: a one-shot wave (54 and up) over a sine two octaves up */
        p->osc[0].wave = 60;
        p->osc[1].wave = 2; p->osc[1].octave = 2; p->osc[1].level = 30; p->osc[1].output = 1;
        p->cutoff = 110;
        p->env[3].l1 = 63;
        p->env[3].t2 = 45; p->env[3].t3 = 30; p->env[3].t4 = 30; p->env[3].lv = 40;
        break;
    }
    for (k = 0; k < 6; k++)
        p->name[k] = TEST_NAME[i][k];
    p->name[6] = 0;
}

/* a flash bank: "SQBK", the count, then 40 programs of 102 bytes */
static const uint8_t *bank_data(uint32_t slot, uint32_t *count)
{
    const uint8_t *b = plat_data(BANK_LO + slot * SECTOR);
    uint32_t magic, n;
    if (!b)
        return 0;
    magic = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    n = b[4];
    if (magic != BANK_MAGIC || n == 0 || n > NPROG)
        return 0;
    if (count)
        *count = n;
    return b + 8;
}
static uint32_t bank_count(uint32_t bank)    /* programs in bank (0: T) */
{
    uint32_t n = 0;
    if (bank == 0)
        return NTEST;
    return bank_data(bank - 1u, &n) ? n : 0u;
}

/* the main loop's events for the audio interrupt: one writer (the main loop), one reader (app_render) */
enum { Q_ON, Q_OFF, Q_PATCH, Q_EDIT, Q_ALLOFF };
#define NEV 64u
static volatile uint32_t ev_q[NEV];
static volatile uint32_t ev_w, ev_r;
static void post(uint32_t kind, uint32_t a, uint32_t b)
{
    uint32_t w = ev_w;
    if (w - ev_r >= NEV)
        return;
    ev_q[w % NEV] = kind | a << 8 | b << 16;
    ev_w = w + 1u;
}

static void apply_knobs(void)                /* the playing program, the knobs' offsets on the stored one */
{
    sq80_patch *p = &play_patch[play_ix ^ 1u];
    *p = base_patch;
    p->cutoff = (uint8_t)clamp((int32_t)p->cutoff + knob[0], 0, 127);
    p->resonance = (uint8_t)clamp((int32_t)p->resonance + knob[1], 0, 31);
    p->env[3].t1 = (uint8_t)clamp((int32_t)p->env[3].t1 + knob[2], 0, 63);
    p->env[3].t4 = (uint8_t)clamp((int32_t)p->env[3].t4 + knob[3], 0, 63);
    play_ix ^= 1u;
}

static void program_load(int fresh)          /* set.bank / set.prog into the engine; fresh: the knobs back to 0 */
{
    uint32_t n = bank_count(set.bank), i;
    const uint8_t *b;
    if (n == 0) {                            /* the bank went: back to T */
        set.bank = 0;
        n = NTEST;
    }
    if (set.prog >= n)
        set.prog = 0;
    if (set.bank == 0)
        test_patch(set.prog, &base_patch);
    else if ((b = bank_data(set.bank - 1u, 0)) != 0)
        sq80_decode_patch(b + 102u * set.prog, &base_patch);
    for (i = 0; i < 6; i++) {
        char c = base_patch.name[i];
        prog_name[i] = c >= 32 && c < 127 ? c : ' ';
    }
    prog_name[6] = 0;
    if (fresh) {
        for (i = 0; i < 4; i++)
            knob[i] = 0;
        edited = 0;
    }
    apply_knobs();
    post(Q_PATCH, play_ix, 0);
}

static void program_from_sysex(const uint8_t *prog102)   /* an ESQ-1/SQ-80 single-program dump: played, unsaved */
{
    uint32_t i;
    sq80_decode_patch(prog102, &base_patch);
    for (i = 0; i < 6; i++) {
        char c = base_patch.name[i];
        prog_name[i] = c >= 32 && c < 127 ? c : ' ';
    }
    prog_name[6] = 0;
    for (i = 0; i < 4; i++)
        knob[i] = 0;
    edited = 1;
    apply_knobs();
    post(Q_PATCH, play_ix, 0);
}

static volatile int32_t last_note = -1;
static volatile uint32_t midi_last_port, midi_count;
static volatile uint32_t pc_req = 0xFFFFFFFFu;   /* a MIDI program change for the main loop */

static void midi_in(void)                    /* audio interrupt */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (st != 0xF8u && st != 0xFEu) {
            midi_last_port = (uint32_t)port;
            midi_count++;
        }
        switch (st & 0xF0u) {
        case 0x90u:
            if (d2) {
                sq80_note_on(&syn, (int)d1, (int)d2);
                last_note = (int32_t)d1;
            } else {
                sq80_note_off(&syn, (int)d1);
            }
            break;
        case 0x80u:
            sq80_note_off(&syn, (int)d1);
            break;
        case 0xB0u:
            sq80_control_change(&syn, (int)d1, (int)d2);
            break;
        case 0xC0u:
            pc_req = d1;
            break;
        case 0xD0u:
            sq80_channel_pressure(&syn, (int)d1);
            break;
        case 0xE0u:
            sq80_pitch_bend(&syn, (int)(d1 | d2 << 7));
            break;
        default:
            break;
        }
    }
}

static void events(void)                     /* audio interrupt */
{
    while (ev_r != ev_w) {
        uint32_t e = ev_q[ev_r % NEV], a = (e >> 8) & 0xFFu, b = e >> 16;
        switch (e & 0xFFu) {
        case Q_ON:
            sq80_note_on(&syn, (int)a, (int)b);
            last_note = (int32_t)a;
            break;
        case Q_OFF:
            sq80_note_off(&syn, (int)a);
            break;
        case Q_PATCH:
            sq80_all_notes_off(&syn);
            sq80_set_patch(&syn, &play_patch[a & 1u]);
            break;
        case Q_EDIT:                         /* the knobs: the notes go on */
            sq80_set_patch(&syn, &play_patch[a & 1u]);
            break;
        case Q_ALLOFF:
            sq80_all_notes_off(&syn);
            break;
        }
        ev_r = ev_r + 1u;
    }
}

static volatile uint32_t voices_now;

static void core2_job(uint32_t n)            /* the second core: voices 4..7 */
{
    sq80_render_part(&syn, buf2_l, buf2_r, (int)n, SQ80_VOICES / 2, SQ80_VOICES / 2);
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    if (n > APP_BLOCK)
        n = APP_BLOCK;
    if (!engine_on) {                        /* the ROM pack is being rewritten: nothing from the flash */
        uint32_t pkt;
        int port;
        while (plat_midi_in(&pkt, &port))
            ;
        for (i = 0; i < 2u * n; i++)
            lr[i] = 0;
        return;
    }
    events();
    midi_in();
    if (two_cores && plat_cpu2_on() && plat_cpu2_run(core2_job, n)) {
        sq80_render_part(&syn, buf_l, buf_r, (int)n, 0, SQ80_VOICES / 2);
        if (plat_cpu2_wait() != 0)
            sq80_render_part(&syn, buf2_l, buf2_r, (int)n, SQ80_VOICES / 2, SQ80_VOICES / 2);
        for (i = 0; i < n; i++) {
            buf_l[i] += buf2_l[i];
            buf_r[i] += buf2_r[i];
        }
        sq80_render_commit(&syn, (int)n);
    } else {
        sq80_render(&syn, buf_l, buf_r, (int)n);
    }
    for (i = 0; i < n; i++) {
        float l, r;
        gain_now += (gain - gain_now) * 0.004f;
        l = buf_l[i] * 0.8f * gain_now;
        r = buf_r[i] * 0.8f * gain_now;
        l = l > 8388607.0f ? 8388607.0f : l < -8388607.0f ? -8388607.0f : l;
        r = r > 8388607.0f ? 8388607.0f : r < -8388607.0f ? -8388607.0f : r;
        lr[2 * i] = (int32_t)l;
        lr[2 * i + 1] = (int32_t)r;
    }
    voices_now = (uint32_t)sq80_active_voices(&syn);
}

void app_panic(void)
{
    uint32_t i;
    for (i = 0; i < SQ80_VOICES; i++)
        syn.voices[i].active = 0;
}

/* the waves: the player's ROM pack, else the test pack (written to TEST_LO the first time) */
static int rom_attach(void)
{
    const uint8_t *p = plat_data(ROM_LO);
    if (p && sq80_rompack_load(&rom, p, ROM_MAX)) {
        rom_real = 1;
        return 1;
    }
    rom_real = 0;
    p = plat_data(TEST_LO);
    if (p && sq80_rompack_load(&rom, p, SQ80_TEST_PACK_SIZE))
        return 1;
    {   /* written once, 512 bytes at a time (no room in RAM for the whole) */
        uint8_t tmp[512];
        uint32_t off;
        for (off = 0; off < SQ80_TEST_PACK_SIZE; off += SECTOR)
            if (plat_data_erase(TEST_LO + off) != 0)
                return 0;
        for (off = 0; off < SQ80_TEST_PACK_SIZE; off += sizeof tmp) {
            uint32_t m = SQ80_TEST_PACK_SIZE - off < sizeof tmp ? SQ80_TEST_PACK_SIZE - off : sizeof tmp;
            sq80_test_pack_part(tmp, off, m);
            if (plat_data_write(TEST_LO + off, tmp, m) != 0)
                return 0;
        }
    }
    p = plat_data(TEST_LO);
    return p && sq80_rompack_load(&rom, p, SQ80_TEST_PACK_SIZE);
}

static void settings_defaults(void)          /* (bank_count works from app_init on: the flash is mapped) */
{
    uint32_t b;
    set.magic = SET_MAGIC;
    set.octave = 0;
    set.bank = 0;
    set.prog = 0;
    for (b = 1; b <= NBANK && !set.bank; b++)    /* the first loaded bank, else T */
        if (bank_count(b))
            set.bank = (uint8_t)b;
}

void app_init(void)
{
    rom_attach();
    sq80_init(&syn, &rom, 44100.0f);
    two_cores = plat_cpu2_set(1) == 0;
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.octave < -2 || set.octave > 2 || set.bank > NBANK || set.prog >= NPROG)
        settings_defaults();
    program_load(1);
    events();                                /* (no audio yet: the program in place before the first render) */
    engine_on = 1;
}

/* ------------------------------------------------------------ the loader --- */
/* SysEx from the loader page, F0 7D 53 51 CMD ... F7 (7D: non-commercial; "SQ"), each answered
 * F0 7D 53 51 7F CMD STATUS E0 E1 [WHY: 3 numbers, 4 x 7 bits each] F7 (E0 E1: the frame's check, below, echoed) (0 done; 1 bad frame; 2 flash refused (why: the
 * offset); 3 CRC (why: the FM-1's CRC, low 28 bits and high 4, the bytes it took); 4 not ready; 5 a byte read back
 * different (why: its offset, written, read); 6 the pack refused (why: its first bytes, the size); 7 the frame's
 * check failed):
 *   01                         hello: the answer's STATUS is 0, then the ROM pack's state (0 none, 1 loaded)
 *   02                         the ROM pack kept: answered 7F 02 00 0 0 SIZE (4 x 7 bits) CRC-32 (5 x 7), 0 for none
 *   10 S0 S1 S2 S3             ROM pack, its size (7 bits each, low first): the sound stops, the old pack goes
 *   11 O0 O1 O2 O3 DATA..      its bytes from offset O, 7-bit packed (8 bytes carry 7: the first the high bits)
 *   12 C0 C1 C2 C3 C4          the end, its CRC-32 (5 x 7 bits): checked, then kept (its first 8 bytes written last)
 *   13 O0 O1 O2 O3 N           read back N (<= 112) bytes of the pack area from offset O: answered 7E O0..O3 DATA
 *   20 B                       bank B (0..3): erased
 *   21 B P DATA..              program P (0..39) of bank B, 102 bytes as 204 nibbles, low first (as ESQ-1 SysEx)
 *   22 B N                     bank B holds N programs: kept
 * Frames 10..12 and 20..22 end with a check: the sum of the bytes from CMD on, mod 16384, as 2 x 7 bits (low first);
 * one that fails it is answered 7 and taken no further (the loader sends it again; every frame may come twice).
 * And an ESQ-1/SQ-80 single-program dump, F0 0F 02 ch 01 (204 nibbles) F7: played, unsaved. */
static struct {
    uint32_t size, erased_to;            /* the pack being written; the sectors erased so far end at erased_to */
    uint8_t head[8];                     /* its first bytes, written last */
    uint8_t active;
    uint32_t got;                        /* bytes written */
} ld;
static uint32_t loads;                       /* frames taken (the screen) */
static uint8_t why[12];                      /* a failure's details, sent with its answer (why_n bytes) */
static uint32_t why_n;
static void why_set(uint32_t a, uint32_t b, uint32_t c)   /* three numbers, 4 x 7 bits each (low first) */
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        why[i] = (uint8_t)((a >> (7u * i)) & 0x7Fu);
        why[4 + i] = (uint8_t)((b >> (7u * i)) & 0x7Fu);
        why[8 + i] = (uint8_t)((c >> (7u * i)) & 0x7Fu);
    }
    why_n = 12;
}
static char ld_msg[24];

static uint8_t echo[2];                      /* the frame's check, sent back with its answer: which frame it answers */
static void reply(uint32_t cmd, uint32_t status)   /* with the failure's details, if any */
{
    uint8_t f[10 + 12] = {0xF0, 0x7D, 0x53, 0x51, 0x7F, 0, 0};
    uint32_t n = 9, i;
    f[5] = (uint8_t)cmd;
    f[6] = (uint8_t)status;
    f[7] = echo[0];
    f[8] = echo[1];
    if (status)
        for (i = 0; i < why_n; i++)
            f[n++] = why[i];
    f[n++] = 0xF7;
    why_n = 0;
    plat_sysex_send(f, n);
}

static uint32_t crc32_flash(const uint8_t *p, uint32_t n, const uint8_t *head)
{
    uint32_t c = 0xFFFFFFFFu, i, k;
    for (i = 0; i < n; i++) {
        c ^= i < 8u ? head[i] : p[i];
        for (k = 0; k < 8u; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint32_t u7(const uint8_t *p, uint32_t n)   /* n 7-bit groups, low first */
{
    uint32_t v = 0, i;
    for (i = 0; i < n; i++)
        v |= (uint32_t)(p[i] & 0x7Fu) << (7u * i);
    return v;
}

static int rom_write(uint32_t off, const uint8_t *src, uint32_t n)   /* erasing ahead as it goes */
{
    while (ld.erased_to < ROM_LO + off + n) {
        if (plat_data_erase(ld.erased_to) != 0)
            return -1;
        ld.erased_to += SECTOR;
    }
    if (off < 8u) {                          /* the head waits for the end */
        uint32_t k = 8u - off < n ? 8u - off : n, i;
        for (i = 0; i < k; i++)
            ld.head[off + i] = src[i];
        off += k;
        src += k;
        n -= k;
    }
    if (!n)
        return 0;
    if (plat_data_write(ROM_LO + off, src, n) != 0) {
        why_set(off, 0, 0);
        return -1;
    }
    {   /* read back: what the flash holds now */
        const uint8_t *f = plat_data(ROM_LO + off);
        uint32_t i;
        for (i = 0; f && i < n; i++)
            if (f[i] != src[i]) {
                why_set(off + i, src[i], f[i]);
                return -2;
            }
    }
    return 0;
}

static void loader_stop_sound(void)
{
    engine_on = 0;
    app_panic();
}

static void loader_resume(void)
{
    rom_attach();
    sq80_init(&syn, &rom, 44100.0f);
    ev_r = ev_w;                             /* (anything posted meanwhile: dropped) */
    program_load(1);
    events();
    engine_on = 1;
}

static int frame_rom(const uint8_t *p, uint32_t n)
{
    uint32_t cmd = p[3];
    if (cmd == 0x10u) {
        if (n < 8u)
            return 1;
        ld.size = u7(p + 4, 4);
        if (ld.size < 64u || ld.size > ROM_MAX)
            return 1;
        loader_stop_sound();
        ld.got = 0;
        ld.active = 1;
        if (plat_data_erase(ROM_LO) != 0)    /* the old pack goes now (the rest is erased as the bytes come) */
            return 2;
        ld.erased_to = ROM_LO + SECTOR;
        return 0;
    }
    if (!ld.active)
        return 4;
    if (cmd == 0x11u) {
        uint8_t raw[7 * 64];
        uint32_t off, m = 0, i;
        if (n < 8u)
            return 1;
        off = u7(p + 4, 4);
        for (i = 8; i < n; i += 8u) {        /* 7-bit packed: a byte of high bits, then up to 7 */
            uint32_t hi = p[i], k;
            for (k = 1; k < 8u && i + k < n && m < sizeof raw; k++)
                raw[m++] = (uint8_t)(p[i + k] | ((hi >> (k - 1u)) & 1u) << 7);
        }
        if (off + m > ld.size)
            return 1;
        {
            int rc = rom_write(off, raw, m);
            if (rc == -2)
                return 5;                    /* written, read back different (why: where, wrote, read) */
            if (rc != 0)
                return 2;
        }
        ld.got += m;
        return 0;
    }
    if (cmd == 0x12u) {
        uint32_t want, got;
        const uint8_t *f = plat_data(ROM_LO);
        sq80_rompack test;
        if (n < 9u || !f)
            return 1;
        want = u7(p + 4, 5);
        ld.active = 0;
        got = crc32_flash(f, ld.size, ld.head);
        if (got != want) {
            why_set(got & 0x0FFFFFFFu, got >> 28, ld.got);   /* the CRC the FM-1 found, the bytes it took */
            loader_resume();                 /* (no pack now: the test waves) */
            return 3;
        }
        if (plat_data_write(ROM_LO, ld.head, 8) != 0) {
            loader_resume();
            return 2;
        }
        if (!sq80_rompack_load(&test, plat_data(ROM_LO), ld.size)) {
            const uint8_t *h = plat_data(ROM_LO);
            why_set(h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16, h[3], ld.size);
            loader_resume();
            return 6;
        }
        loader_resume();
        return 0;
    }
    return 1;
}

static int frame_bank(const uint8_t *p, uint32_t n)
{
    uint32_t cmd = p[3], b = n > 4u ? p[4] : 0xFFu;
    if (b >= NBANK)
        return 1;
    if (cmd == 0x20u) {                      /* (a program playing from it stays: it is in RAM) */
        return plat_data_erase(BANK_LO + b * SECTOR) != 0 ? 2 : 0;
    }
    if (cmd == 0x21u) {
        uint8_t prog[102];
        uint32_t pi = n > 5u ? p[5] : 0xFFu, i;
        if (pi >= NPROG || n < 6u + 204u)
            return 1;
        for (i = 0; i < 102u; i++)
            prog[i] = (uint8_t)((p[6 + 2 * i] & 15u) | (p[7 + 2 * i] & 15u) << 4);
        return plat_data_write(BANK_LO + b * SECTOR + 8u + 102u * pi, prog, 102) != 0 ? 2 : 0;
    }
    if (cmd == 0x22u) {
        uint8_t h[8] = {0x53, 0x51, 0x42, 0x4B, 0, 0, 0, 0};
        uint32_t cnt = n > 5u ? p[5] : 0u;
        if (cnt == 0 || cnt > NPROG)
            return 1;
        h[4] = (uint8_t)cnt;
        if (plat_data_write(BANK_LO + b * SECTOR, h, 8) != 0)
            return 2;
        if (set.bank == b + 1u || set.bank == 0) {   /* from the test sounds to the player's first bank */
            if (set.bank == 0) {
                set.bank = (uint8_t)(b + 1u);
                set.prog = 0;
            }
            program_load(1);
        }
        return 0;
    }
    return 1;
}

int app_sysex(const uint8_t *p, uint32_t n)
{
    int st;
    if (n == 4u + 204u && p[0] == 0x0Fu && p[1] == 0x02u && p[3] == 0x01u) {   /* ESQ-1 / SQ-80 single program */
        uint8_t prog[102];
        uint32_t i;
        for (i = 0; i < 102u; i++)
            prog[i] = (uint8_t)((p[4 + 2 * i] & 15u) | (p[5 + 2 * i] & 15u) << 4);
        program_from_sysex(prog);
        app_say("PROGRAM", prog_name);
        return 1;
    }
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x53u || p[2] != 0x51u)
        return 0;
    loads++;
    echo[0] = echo[1] = 0;
    if ((p[3] >= 0x10u && p[3] <= 0x12u) || (p[3] >= 0x20u && p[3] <= 0x22u)) {   /* their last 2 bytes: a check */
        uint32_t sum = 0, i;
        if (n < 6u) {
            reply(p[3], 1);
            return 1;
        }
        for (i = 3; i < n - 2u; i++)
            sum += p[i];
        echo[0] = p[n - 2u];
        echo[1] = p[n - 1u];
        if ((sum & 0x7Fu) != p[n - 2u] || ((sum >> 7) & 0x7Fu) != p[n - 1u]) {
            reply(p[3], 7);                  /* damaged on the way: the loader sends it again */
            return 1;
        }
        n -= 2u;
    }
    switch (p[3]) {
    case 0x01u: {
        uint8_t f[9] = {0xF0, 0x7D, 0x53, 0x51, 0x7F, 0x01, 0, 0, 0xF7};
        f[7] = rom_real;
        plat_sysex_send(f, sizeof f);
        return 1;
    }
    case 0x02u: {                        /* the ROM pack kept: 7F 02 00 0 0 SIZE(4 x 7) CRC(5 x 7), zeros for none */
        uint8_t f[19] = {0xF0, 0x7D, 0x53, 0x51, 0x7F, 0x02, 0, 0, 0};
        uint32_t size = 0, crc = 0, i;
        const uint8_t *h = plat_data(ROM_LO);
        if (rom_real && h) {
            uint32_t raws = h[20] | (uint32_t)h[21] << 8, romsz = h[24] | (uint32_t)h[25] << 8 | (uint32_t)h[26] << 16,
                     body = 6u * SQ80_WAVES + 16u * SQ80_WAVES + 4u * raws;
            body += (16u - body % 16u) % 16u;
            size = 32u + body + romsz;
            if (size <= ROM_MAX)
                crc = crc32_flash(h, size, h);
            else
                size = 0;
        }
        for (i = 0; i < 4u; i++)
            f[9 + i] = (uint8_t)((size >> (7u * i)) & 0x7Fu);
        for (i = 0; i < 5u; i++)
            f[13 + i] = (uint8_t)((crc >> (7u * i)) & 0x7Fu);
        f[18] = 0xF7;
        plat_sysex_send(f, sizeof f);
        return 1;
    }
    case 0x13u: {                        /* read the flash back (the pack's first 8 bytes: 0xFF until it is kept):
                                          * 13 O0..O3 N -> 7E O0..O3 DATA (7-bit packed), N <= 112 */
        uint8_t f[12 + 128];
        const uint8_t *src;
        uint32_t off = n >= 9u ? u7(p + 4, 4) : 0u, cnt = n >= 9u ? p[8] : 0u, k = 0, i;
        if (cnt == 0 || cnt > 112u || (src = plat_data(ROM_LO + off)) == 0 || off + cnt > ROM_MAX) {
            reply(0x13, 1);
            return 1;
        }
        f[0] = 0xF0; f[1] = 0x7D; f[2] = 0x53; f[3] = 0x51; f[4] = 0x7E;
        for (i = 0; i < 4u; i++)
            f[5 + i] = p[4 + i] & 0x7Fu;
        k = 9;
        for (i = 0; i < cnt; i += 7u) {
            uint32_t j, hi = 0;
            for (j = 0; j < 7u && i + j < cnt; j++)
                hi |= (uint32_t)(src[i + j] >> 7) << j;
            f[k++] = (uint8_t)hi;
            for (j = 0; j < 7u && i + j < cnt; j++)
                f[k++] = src[i + j] & 0x7Fu;
        }
        f[k++] = 0xF7;
        plat_sysex_send(f, k);
        return 1;
    }
    case 0x10u: case 0x11u: case 0x12u:
        st = frame_rom(p, n);
        if (p[3] == 0x12u) {
            app_say("ROM PACK", st == 0 ? "LOADED" : "FAILED");
            ld_msg[0] = 0;
        }
        break;
    case 0x20u: case 0x21u: case 0x22u:
        st = frame_bank(p, n);
        if (p[3] == 0x22u)
            app_say("BANK", st == 0 ? "LOADED" : "FAILED");
        break;
    default:
        st = 1;
        break;
    }
    reply(p[3], (uint32_t)st);
    return 1;
}

/* --------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint8_t stress;
    uint32_t peak;
    uint32_t cpu_shown, voices_shown, peak_shown, loads_shown;
    uint8_t redraw;
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void bank_text(char *t)               /* "T" or "1".."4" */
{
    t[0] = set.bank == 0 ? 'T' : (char)('0' + set.bank);
    t[1] = 0;
}

static void prog_text(char *t)               /* "T-01 SAWPAD" */
{
    char n[12];
    bank_text(t);
    text_cat(t, "-");
    if (set.prog + 1u < 10u)
        text_cat(t, "0");
    int_text(set.prog + 1, n);
    text_cat(t, n);
    text_cat(t, " ");
    text_cat(t, prog_name);
}

static void speak(void)
{
    char t[96], n[24];
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME ". PROGRAM ");
        prog_text(n);
        text_cat(t, n);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

static const int32_t CHORD[SQ80_VOICES] = {48, 55, 60, 64, 67, 72, 76, 79};

static void stress_set(int on)
{
    uint32_t i;
    if (on == ui.stress)
        return;
    ui.stress = (uint8_t)on;
    for (i = 0; i < SQ80_VOICES; i++)
        post(on ? Q_ON : Q_OFF, (uint32_t)CHORD[i], 100u);
    ui.peak = 0;
    app_say("STRESS", on ? "8 VOICES" : "OFF");
}

static void octave_step(int32_t d)
{
    int32_t o = clamp(set.octave + d, -2, 2);
    char t[4];
    if (o == set.octave)
        return;
    post(Q_ALLOFF, 0, 0);
    set.octave = (int8_t)o;
    set_dirty = 1;
    t[0] = o < 0 ? '-' : '+';
    t[1] = (char)('0' + (o < 0 ? -o : o));
    t[2] = 0;
    app_say("OCTAVE", t);
}

static void program_step(int32_t d)          /* PRESET: through the bank, then on to the next one that has any */
{
    int32_t p = (int32_t)set.prog + d, b = set.bank, tries;
    char t[24];
    for (tries = 0; tries < 12; tries++) {
        int32_t n = (int32_t)bank_count((uint32_t)b);
        if (n && p >= 0 && p < n)
            break;
        if (p < 0) {
            b = b == 0 ? (int32_t)NBANK : b - 1;
            p = (int32_t)bank_count((uint32_t)b) - 1;
        } else {
            b = b == (int32_t)NBANK ? 0 : b + 1;
            p = 0;
        }
    }
    set.bank = (uint8_t)b;
    set.prog = (uint8_t)(p < 0 ? 0 : p);
    set_dirty = 1;
    ui.stress = 0;
    program_load(1);
    prog_text(t);
    app_say("", t);
}

static void bank_step(int32_t d)             /* SELECT: the next bank that has programs */
{
    int32_t b = set.bank, tries;
    char t[24];
    for (tries = 0; tries < (int32_t)NBANK + 1; tries++) {
        b = (b + d + (int32_t)NBANK + 1) % ((int32_t)NBANK + 1);
        if (bank_count((uint32_t)b))
            break;
    }
    if (b == set.bank)
        return;
    set.bank = (uint8_t)b;
    set.prog = 0;
    set_dirty = 1;
    ui.stress = 0;
    program_load(1);
    t[0] = 0;
    text_cat(t, "BANK ");
    bank_text(t + 5);
    app_say(t, prog_name);
}

static const char *const KNOB_NAME[4] = {"CUTOFF", "RESON", "ATTACK", "RELEASE"};
static int32_t knob_value(uint32_t k)        /* what the playing program has now */
{
    const sq80_patch *p = &play_patch[play_ix];
    return k == 0 ? p->cutoff : k == 1 ? p->resonance : k == 2 ? p->env[3].t1 : p->env[3].t4;
}

static void knob_turn(uint32_t k, int32_t e)
{
    static const int8_t LIM[4] = {127, 31, 63, 63};
    char t[12];
    knob[k] = (int8_t)clamp(knob[k] + e, -LIM[k], LIM[k]);
    edited = 1;
    apply_knobs();
    post(Q_EDIT, play_ix, 0);
    int_text(knob_value(k), t);
    app_say(KNOB_NAME[k], t);
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {            /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k, v;
        int32_t note = 53 + (int32_t)k + 12 * set.octave;
        if ((keys & bit) && !(ui.keys & bit)) {
            v = touch_vel(k);
            if (v < 1u)
                v = 1u;
            post(Q_ON, (uint32_t)note, v);
            plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)note << 16 | v << 24, TO_USB | TO_BLE);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            post(Q_OFF, (uint32_t)note, 0);
            plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)note << 16, TO_USB | TO_BLE);
        }
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (k == B_OCTDN)
            octave_step(-1);
        else if (k == B_OCTUP)
            octave_step(1);
        else if (k == B_ARP)
            stress_set(!ui.stress);
        else if (k == B_HOME) {
            uint32_t i;
            for (i = 0; i < 4; i++)
                knob[i] = 0;
            edited = 0;
            apply_knobs();
            post(Q_EDIT, play_ix, 0);
            ui.peak = 0;
            app_say("PROGRAM", "AS STORED");
        } else if (k == B_SAVE) {
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SAVED", "");
            } else {
                app_say("SAVE", "FAILED");
            }
        }
    }
    if ((e = plat_enc(EN_PRESET)) != 0)
        program_step(e > 0 ? 1 : -1);
    if ((e = plat_enc(EN_SELECT)) != 0)
        bank_step(e > 0 ? 1 : -1);
    (void)plat_enc(EN_ALGO);
    for (k = 0; k < 4u; k++)
        if ((e = plat_enc((int)(EN_K1 + k))) != 0)
            knob_turn(k, e);
    if (pc_req != 0xFFFFFFFFu) {             /* MIDI program change: 0..39 in this bank */
        uint32_t pc = pc_req;
        pc_req = 0xFFFFFFFFu;
        if (pc < bank_count(set.bank)) {
            set.prog = (uint8_t)pc;
            program_load(1);
            ui.redraw = 1;
        }
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);
    plat_leds(btn | (set_dirty ? 1u << B_SAVE : 0u) | (ui.stress ? 1u << B_ARP : 0u) |
              (edited ? 1u << B_HOME : 0u), keys);
}

void app_input_only(void) { input(); }

static void note_name(int32_t note, char *b)
{
    static const char *const N[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char o[12];
    b[0] = 0;
    text_cat(b, N[note % 12]);
    int_text(note / 12 - 1, o);
    text_cat(b, o);
}

static void draw(void)
{
    char t[32], n[12];
    uint32_t now = plat_ms(), cpu = plat_cpu_pct(), k;
    /* the header: the name, or a message for a moment */
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(ui.msg[0][0] ? x + 8 : 10, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        cv_text(10, 6, &FONT_B, APP_NAME, C_WHITE);
        if (last_note >= 0) {
            note_name(last_note, t);
            cv_text(230 - text_w(&FONT_B, t), 6, &FONT_B, t, C_HI);
        }
    }
    cv_blit(0, 0);
    /* the program */
    cv_begin(240, 64, C_BLACK);
    cv_text(10, 2, &FONT_XS, "BANK", C_GRAY);
    bank_text(t);
    cv_text(44, 2, &FONT_XS, t, C_HI);
    cv_text(70, 2, &FONT_XS, "PROGRAM", C_GRAY);
    int_text(set.prog + 1, t);
    text_cat(t, " / ");
    int_text((int32_t)bank_count(set.bank), n);
    text_cat(t, n);
    cv_text(124, 2, &FONT_XS, t, C_HI);
    cv_text(10, 20, &FONT_L, prog_name, edited ? C_HI : C_WHITE);
    cv_blit(0, 34);
    /* the knobs */
    cv_begin(240, 46, C_BLACK);
    for (k = 0; k < 4u; k++) {
        int32_t x = 4 + (int32_t)k * 59, v = knob_value(k), lim = k == 0 ? 127 : k == 1 ? 31 : 63;
        cv_text(x, 0, &FONT_XS, KNOB_NAME[k], C_GRAY);
        int_text(v, t);
        cv_text(x, 14, &FONT_B, t, knob[k] ? C_HI : C_WHITE);
        cv_rect(x, 36, 54, 4, C_LINE);
        cv_rect(x, 36, 54 * v / lim, 4, knob[k] ? C_HI : C_GRAY);
    }
    cv_blit(0, 100);
    /* the state: the waves, the load, MIDI */
    cv_begin(240, 92, C_BLACK);
    cv_text(10, 2, &FONT_XS, "WAVES", C_GRAY);
    cv_text(66, 2, &FONT_XS, rom_real ? "SQ-80 ROM" : "TEST (NO ROM PACK)", rom_real ? C_WHITE : C_HI);
    cv_text(10, 20, &FONT_XS, "CPU", C_GRAY);
    int_text((int32_t)cpu, t);
    text_cat(t, "%  PEAK ");
    int_text((int32_t)ui.peak, n);
    text_cat(t, n);
    text_cat(t, "%");
    cv_text(66, 20, &FONT_XS, t, cpu >= 85u ? RGB(230, 70, 60) : C_WHITE);
    cv_text(10, 38, &FONT_XS, "VOICES", C_GRAY);
    int_text((int32_t)voices_now, t);
    text_cat(t, two_cores && plat_cpu2_on() ? "   2 CORES" : "   1 CORE");
    cv_text(66, 38, &FONT_XS, t, C_WHITE);
    cv_text(10, 56, &FONT_XS, "OCT", C_GRAY);
    t[0] = set.octave < 0 ? '-' : '+';
    t[1] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
    t[2] = 0;
    cv_text(66, 56, &FONT_XS, t, C_HI);
    if (midi_count) {
        static const char *const PORT[NPORTS] = {"USB", "BLE", "TRS"};
        cv_text(120, 56, &FONT_XS, "MIDI", C_GRAY);
        cv_text(156, 56, &FONT_XS, midi_last_port < NPORTS ? PORT[midi_last_port] : "", C_HI);
    }
    if (ld.active) {
        int_text((int32_t)(ld.got * 100u / (ld.size ? ld.size : 1u)), t);
        text_cat(t, "%");
        cv_text(10, 74, &FONT_XS, "LOADING ROM PACK", C_HI);
        cv_text(140, 74, &FONT_XS, t, C_WHITE);
    } else {
        cv_text(10, 74, &FONT_XS, "SELECT: BANK   PRESET: PROGRAM", C_DIM);
    }
    cv_blit(0, 148);
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_midi, last_tick;
    uint32_t now = plat_ms(), cpu = plat_cpu_pct(), v = voices_now;
    input();
    speak();
    if (cpu > ui.peak)
        ui.peak = cpu;
    if (midi_count != last_midi) {
        last_midi = midi_count;
        ui.redraw = 1;
    }
    if (now - last_tick >= 250u &&
        (cpu != ui.cpu_shown || v != ui.voices_shown || ui.peak != ui.peak_shown || loads != ui.loads_shown)) {
        last_tick = now;
        ui.cpu_shown = cpu;
        ui.voices_shown = v;
        ui.peak_shown = ui.peak;
        ui.loads_shown = loads;
        ui.redraw = 1;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "bank"))
        *got = set.bank;
    else if (!strcmp(what, "prog"))
        *got = set.prog;
    else if (!strcmp(what, "banksize"))
        *got = (int)bank_count(set.bank);
    else if (!strcmp(what, "rom"))
        *got = rom_real;
    else if (!strcmp(what, "voices"))
        *got = (int)voices_now;
    else if (!strcmp(what, "note"))
        *got = last_note;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "cutoff"))
        *got = play_patch[play_ix].cutoff;
    else if (!strcmp(what, "name0"))         /* the program name's first letter */
        *got = prog_name[0];
    else if (!strcmp(what, "cores"))
        *got = two_cores && plat_cpu2_on() ? 2 : 1;
    else if (!strcmp(what, "stress"))
        *got = ui.stress;
    else
        return 0;
    return 1;
}
#endif
