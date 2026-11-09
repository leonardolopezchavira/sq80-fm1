/* SPDX-License-Identifier: MIT
 * SQ-80 for the M-VAVE FM-1 (an fm1-base app; started from fm1-base's template, MPL-2.0).
 *
 * The sq80 engine plays the player's own SQ-80 waves and programs, kept in the FM-1's flash:
 *   - the ROM pack (tools/make_rompack.py, from the player's SQ-80 OS and wave ROM dumps) at ROM_LO;
 *   - eight banks of 40 programs (ESQ-1 / SQ-80 format, 102 bytes each), one flash sector each: banks 1..4 at
 *     BANK_LO, banks 5..8 in the XDATA0 window (bank_base: there bytes 0xF00..0xF07 of each sector stay erased).
 * Both come in over USB SysEx from the loader page (web/load/index.html) in this app's own framing (below); a
 * single-program ESQ-1/SQ-80 dump (F0 0F 02 ch 01 ...) from any librarian plays at once, unsaved. Without a ROM
 * pack the engine plays its built-in waves: an original set of 75 in the same slots (tools/gen_waves.py, built into
 * the image as waves/sq80_waves.h: no Ensoniq data); where no bank is stored, INIT programs.
 *
 * The panel: PRESET picks the program, SELECT the bank, K1..K4 change the playing program's cutoff, resonance,
 * attack and release (HOME puts them back), OCT- / OCT+ the octave (bank, program, octave and the instrument's
 * settings keep themselves), SAVE stores the program where PRESET / SELECT say; SEL, ENV,
 * LFO, EDIT edit the program, FX the effects (FMini-1's), ARP and SEQ SLOOP's arpeggiator and step sequencer
 * (seq/sloop_seq.c, GPL-3.0: PLAY, REC, the steps on the keys while SEQ is held), GLO the load (PLAY there: a load
 * test). The second core renders voices 4..7 (fm1.toml cpu2).
 *
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in
 * scope. The engine is compiled apart at -O2 (fm1.toml o2). */
#include "sq80.h"
#include "sq80_params.h"
#include "sq80_cal.h"                      /* (SQ80_ENV_TICKS: the envelope drawing's times) */
#include "sq80_random.h"
#include "fmini_fx.h"
#include "sloop_seq.h"
#include "sq80_waves.h"                 /* the built-in waves (tools/gen_waves.py: no Ensoniq data) */
#include "seg_font.h"                   /* the program name's letters (tools/gen_seg_font.py) */

#define C_ACCENT RGB(64, 220, 230)          /* the highlight: cyan, as the boot screen's */

/* ------------------------------------------------------------- flash map --- */
#define SECTOR 4096u
#define ROM_LO 0x97000u                  /* the ROM pack: 65 sectors, to BANK_LO */
#define ROM_MAX (BANK_LO - ROM_LO)
#define BANK_LO 0xD8000u                 /* banks 1..4, a sector each, to the store's objects at 0xDC000 */
#define BANK_HI_LO PLAT_XDATA0_LO        /* banks 5..8: the XDATA0 window's four sectors */
#define NBANK 8u
#define GAP 0xF00u                       /* banks 5..8: bytes GAP..GAP+7 of the sector kept erased (the update loader
                                          * looks for its record there: plat.h, PLAT_XDATA0) */
#define BANK_MAGIC 0x4B425153u           /* "SQBK" */
#define NPROG 40u

/* --------------------------------------------------------------- settings --- */
#define SET_MAGIC 0x53513833u            /* "SQ83": SLOOP's arpeggiator and the pattern (0.9); "SQ82": 0.8's arpeggiator;
                                          * "SQ81": the effects (0.7); "SQ80": neither */
#define SET_MAGIC_0 0x53513830u
#define SET_MAGIC_1 0x53513831u
#define SET_MAGIC_2 0x53513832u
#define SET_ARP_08 8u                    /* 0.8's arpeggiator settings: 8 bytes (its tempo: byte 5, BPM - 40) */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t bank;                        /* 1..8; 0: no bank loaded */
    uint8_t prog;                        /* 0 .. 39 */
    uint8_t rsv;
    uint8_t fx[FXP_N];                   /* the effects (fmini_fx.h): the instrument's, not a program's */
    uint8_t minvel;                      /* the keys' softest velocity, 0..127 (GLO K2; 0: as played) */
    uint8_t rsv2[2];
    sl_song sl;                          /* the arpeggiator's and the sequencer's settings, and the pattern */
} settings_t;
static settings_t set;
static uint8_t set_dirty;

/* ----------------------------------------------------------------- engine --- */
static sq80_rompack rom;
static uint8_t rom_real;                 /* the player's ROM pack (else the built-in waves) */
static volatile uint8_t engine_on;       /* 0 while the ROM pack is rewritten: the render keeps quiet */
static sq80_synth syn;
static float buf_l[APP_BLOCK], buf_r[APP_BLOCK];
static float buf2_l[APP_BLOCK], buf2_r[APP_BLOCK];   /* the second core's voices */
static uint8_t two_cores;

static uint8_t prog_raw[102];            /* the program playing, as its 102 bytes: what the editor changes */
static sq80_patch play_patch[2];         /* decoded: two, the main loop writes the one not playing */
static uint8_t play_ix;
static char prog_name[8];
static uint8_t edited;                   /* changed since it was loaded (not saved) */

/* a flash bank: "SQBK", the count, then 40 programs of 102 bytes; in banks 5..8 the programs step over GAP */
static uint32_t bank_base(uint32_t slot)     /* slot 0..7 */
{
    return slot < 4u ? BANK_LO + slot * SECTOR : BANK_HI_LO + (slot - 4u) * SECTOR;
}
static uint32_t prog_at(uint32_t slot, uint32_t pi)   /* where byte 0 of program pi is (before the gap's step) */
{
    (void)slot;
    return 8u + 102u * pi;
}
static uint32_t phys(uint32_t slot, uint32_t o)       /* a byte's place in the sector: over the gap in banks 5..8 */
{
    return slot >= 4u && o >= GAP ? o + 8u : o;
}
static uint32_t bank_count_slot(uint32_t slot)
{
    const uint8_t *b = plat_data(bank_base(slot));
    uint32_t magic;
    if (!b)
        return 0;
    magic = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return magic == BANK_MAGIC && b[4] >= 1u && b[4] <= NPROG ? b[4] : 0u;
}
static uint32_t bank_stored(uint32_t bank)   /* programs stored in bank 1..8 (0 for none) */
{
    return bank >= 1u && bank <= NBANK ? bank_count_slot(bank - 1u) : 0u;
}
/* every bank shows 40 programs: those stored, then INIT in the places nothing was stored yet (a bank never loaded:
 * 40 INITs); saving into one of them stores it */
static uint32_t bank_count(uint32_t bank) { return bank >= 1u && bank <= NBANK ? NPROG : 0u; }
static int bank_read(uint32_t slot, uint32_t pi, uint8_t out[102])
{
    const uint8_t *b = plat_data(bank_base(slot));
    uint32_t i, o = prog_at(slot, pi);
    if (!b)
        return -1;
    for (i = 0; i < 102u; i++)
        out[i] = b[phys(slot, o + i)];
    return 0;
}
static int bank_write(uint32_t slot, uint32_t pi, const uint8_t prog[102])   /* in up to two pieces, round the gap */
{
    uint32_t o = prog_at(slot, pi), base = bank_base(slot);
    if (slot >= 4u && o < GAP && o + 102u > GAP) {
        uint32_t k = GAP - o;
        if (plat_data_write(base + o, prog, k) != 0)
            return -1;
        return plat_data_write(base + GAP + 8u, prog + k, 102u - k);
    }
    return plat_data_write(base + phys(slot, o), prog, 102);
}
static uint32_t first_bank(void)             /* the first loaded bank, else 1 */
{
    uint32_t b;
    for (b = 1; b <= NBANK; b++)
        if (bank_stored(b))
            return b;
    return 1;
}
static int prog_read(uint32_t bank, uint32_t pi, uint8_t out[102])   /* program pi of bank 1..8, INIT where none */
{
    if (pi < bank_stored(bank))
        return bank_read(bank - 1u, pi, out);
    sq80_init_program(out);
    return 0;
}

/* the main loop's events for the audio interrupt: one writer (the main loop), one reader (app_render) */
enum { Q_ON, Q_OFF, Q_PATCH, Q_EDIT, Q_ALLOFF, Q_SEQ };
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

static void program_play(int fresh)          /* prog_raw to the engine; fresh: a new program (notes off) */
{
    uint32_t i;
    sq80_patch *p = &play_patch[play_ix ^ 1u];
    sq80_decode_patch(prog_raw, p);
    for (i = 0; i < 6; i++) {
        char c = (char)prog_raw[i];
        prog_name[i] = c >= 32 && c < 127 ? c : ' ';
    }
    prog_name[6] = 0;
    play_ix ^= 1u;
    post(fresh ? Q_PATCH : Q_EDIT, play_ix, 0);
}

static void program_load(int fresh)          /* set.bank / set.prog into the engine (fresh: the edits dropped) */
{
    (void)fresh;
    if (set.bank < 1u || set.bank > NBANK)
        set.bank = 1;
    if (set.prog >= NPROG)
        set.prog = 0;
    if (prog_read(set.bank, set.prog, prog_raw) != 0)
        sq80_init_program(prog_raw);
    edited = 0;
    program_play(1);
}

static void program_from_sysex(const uint8_t *prog102)   /* an ESQ-1/SQ-80 single-program dump: played, unsaved */
{
    uint32_t i;
    for (i = 0; i < 102u; i++)
        prog_raw[i] = prog102[i];
    edited = 1;
    program_play(1);
}

static volatile int32_t last_note = -1;
static void last_note_set(int32_t n) { last_note = n; }
static volatile uint32_t midi_last_port, midi_count;
static volatile uint32_t pc_req = 0xFFFFFFFFu;   /* a MIDI program change for the main loop */

/* ---------------------------------------------- the arpeggiator, the sequencer --- */
/* SLOOP's (seq/sloop_seq.c, GPL-3.0): the keys' and MIDI's notes go through it (the arpeggiator takes them when on,
 * the recording writes them), and its notes come back here. A note two of them play at once (a key and a step)
 * sounds once and ends when the last of them lets it go. */
static uint8_t note_cnt[128];

void sl_out_on(uint32_t note, uint32_t vel)
{
    note &= 127u;
    if (note_cnt[note])
        sq80_note_off(&syn, (int)note);      /* (sounding already: again) */
    if (note_cnt[note] < 255u)
        note_cnt[note]++;
    sq80_note_on(&syn, (int)note, (int)(vel ? vel : 1u));
    last_note = (int32_t)note;
}

void sl_out_off(uint32_t note)
{
    note &= 127u;
    if (!note_cnt[note] || --note_cnt[note])
        return;
    sq80_note_off(&syn, (int)note);
}

/* the metronome (GLO: CLICK): a short sine blip, 1.6 kHz on the bar's first beat, 1 kHz on the others, decaying in
 * ~30 ms; mixed after the effects (no reverb on it), started where the beat falls in the block */
static struct {
    float y1, y2, k, env;
    uint32_t left, at;                   /* samples still to sound; where in this block it starts */
} click;
static uint32_t render_at;                   /* app_render: the place in the block reached (sl_run's "now") */

void sl_out_click(uint32_t accent)
{
    click.k = accent ? 1.94824f : 1.97974f;  /* 2 cos(2 pi f / 44100) */
    click.y1 = 0.0f;
    click.y2 = accent ? -0.2268f : -0.1420f; /* (-sin w: the sine starts at 0) */
    click.env = accent ? 18000.0f : 12000.0f;
    click.left = 2646u;                      /* 60 ms */
    click.at = render_at;
}

static void click_mix(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    float g = (float)master_q12 * (1.0f / 4096.0f);
    for (i = click.at; i < n && click.left; i++, click.left--) {
        float y = click.k * click.y1 - click.y2;
        int32_t v, j;
        click.y2 = click.y1;
        click.y1 = y;
        v = (int32_t)(y * click.env * g);
        click.env *= 0.99925f;
        for (j = 0; j < 2; j++) {
            int32_t o = (lr[2u * i + (uint32_t)j] >> 8) + v;
            lr[2u * i + (uint32_t)j] = (o > 32767 ? 32767 : o < -32768 ? -32768 : o) * 256;
        }
    }
    click.at = 0;
}

/* the scope (HOME): the output as heard, the two sides mixed, the last 1024 samples (the main loop reads them) */
#define SCOPE_N 1024u
static int16_t scope_cap[SCOPE_N];
static volatile uint32_t scope_w;
static void scope_take(const int32_t *lr, uint32_t n)
{
    uint32_t i, w = scope_w;
    for (i = 0; i < n; i++)
        scope_cap[(w + i) % SCOPE_N] = (int16_t)(((lr[2u * i] >> 8) + (lr[2u * i + 1u] >> 8)) / 2);
    scope_w = w + n;
}

static void notes_forget(void)               /* the engine let every note go */
{
    memset(note_cnt, 0, sizeof note_cnt);
}

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
        if (st >= 0xF8u) {                   /* real time: the clock, start, stop for the sequencer */
            sl_realtime(st, plat_ms());
            continue;
        }
        switch (st & 0xF0u) {
        case 0x90u:
            sl_key(d1, d2);
            break;
        case 0x80u:
            sl_key(d1, 0);
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
        case Q_ON:                           /* (velocity + 128: past the arpeggiator, the load test's) */
            if (b < 128u) {
                sl_key(a, b);
            } else {
                sq80_note_on(&syn, (int)a, (int)(b & 127u));
                last_note = (int32_t)a;
            }
            break;
        case Q_OFF:
            if (b < 128u)
                sl_key(a, 0);
            else
                sq80_note_off(&syn, (int)a);
            break;
        case Q_SEQ:                          /* the sequencer's commands: a = SC_*, b = step | change << 8 */
            sl_cmd(a, b & 0xFFu, (int32_t)(int8_t)(b >> 8));
            break;
        case Q_PATCH:
            sq80_all_notes_off(&syn);
            notes_forget();
            sq80_set_patch(&syn, &play_patch[a & 1u]);
            break;
        case Q_EDIT:                         /* the knobs: the notes go on */
            sq80_set_patch(&syn, &play_patch[a & 1u]);
            break;
        case Q_ALLOFF:
            sq80_all_notes_off(&syn);
            notes_forget();
            break;
        }
        ev_r = ev_r + 1u;
    }
}

static volatile uint32_t voices_now;

static void core2_job(uint32_t arg)          /* the second core: voices 4..7 of a stretch (offset << 16 | length) */
{
    uint32_t off = arg >> 16, n = arg & 0xFFFFu;
    sq80_render_part(&syn, buf2_l + off, buf2_r + off, (int)n, SQ80_VOICES / 2, SQ80_VOICES / 2);
}

static void render_span(uint32_t off, uint32_t n)   /* the voices over n samples from off, on both cores */
{
    uint32_t i;
    if (two_cores && plat_cpu2_on() && plat_cpu2_run(core2_job, off << 16 | n)) {
        sq80_render_part(&syn, buf_l + off, buf_r + off, (int)n, 0, SQ80_VOICES / 2);
        if (plat_cpu2_wait() != 0)
            sq80_render_part(&syn, buf2_l + off, buf2_r + off, (int)n, SQ80_VOICES / 2, SQ80_VOICES / 2);
        for (i = off; i < off + n; i++) {
            buf_l[i] += buf2_l[i];
            buf_r[i] += buf2_r[i];
        }
        sq80_render_commit(&syn, (int)n);
    } else {
        sq80_render(&syn, buf_l + off, buf_r + off, (int)n);
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, off = 0;
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
    render_at = 0;
    sl_block(n, plat_ms());
    for (i = 0; off < n && i < 64u; i++) {   /* up to each note of the arpeggiator and the sequencer, then the note */
        uint32_t due = sl_next(), m = due < n - off ? due : n - off;
        if (m)
            render_span(off, m);
        off += m;
        render_at = off;
        sl_run(m);
    }
    if (off < n)                             /* (never: a guard against an event that does not move on) */
        render_span(off, n - off);
    fx_render(buf_l, buf_r, lr, n, master_q12);   /* DRIVE, the sends, the effects, the master (fmini_fx.c) */
    if (click.left)
        click_mix(lr, n, master_q12);
    scope_take(lr, n);
    voices_now = (uint32_t)sq80_active_voices(&syn);
}

void app_panic(void)
{
    uint32_t i;
    for (i = 0; i < SQ80_VOICES; i++)
        syn.voices[i].active = 0;
    notes_forget();
}

/* the waves: the player's ROM pack, else the built-in set (waves/sq80_waves.h: tools/gen_waves.py's, our own) */
static int rom_attach(void)
{
    const uint8_t *p = plat_data(ROM_LO);
    if (p && sq80_rompack_load(&rom, p, ROM_MAX)) {
        rom_real = 1;
        return 1;
    }
    rom_real = 0;
    return sq80_rompack_load(&rom, SQ80_WAVES_PACK, SQ80_WAVES_SIZE);
}

static void settings_defaults(void)          /* (the flash is mapped from app_init on) */
{
    set.magic = SET_MAGIC;
    set.octave = 0;
    set.bank = (uint8_t)first_bank();
    set.prog = 0;
    fx_defaults(set.fx);
    sl_defaults(&set.sl);
}

void app_init(void)
{
    rom_attach();
    sq80_init(&syn, &rom, 44100.0f);
    two_cores = plat_cpu2_set(1) == 0;
    {
        int got = plat_store_load(OBJ_SETTINGS, &set, sizeof set), i;
        if (got == 8 && set.magic == SET_MAGIC_0) {   /* 0.3 .. 0.6's: bank, program, octave kept, the effects new */
            set.magic = SET_MAGIC;
            fx_defaults(set.fx);
            sl_defaults(&set.sl);
        } else if (got == (int)(8 + FXP_N) && set.magic == SET_MAGIC_1) {   /* 0.7's: the arpeggiator new */
            set.magic = SET_MAGIC;
            sl_defaults(&set.sl);
        } else if (got == (int)(8 + FXP_N + SET_ARP_08) && set.magic == SET_MAGIC_2) {   /* 0.8's: its tempo kept */
            uint32_t bpm = ((const uint8_t *)&set)[8 + FXP_N + 5] + 40u;
            set.magic = SET_MAGIC;
            sl_defaults(&set.sl);
            if (bpm >= 40u && bpm <= 240u)
                set.sl.p[SL_BPM] = (uint8_t)bpm;
        } else if (got != (int)sizeof set || set.magic != SET_MAGIC) {
            settings_defaults();
        }
        if (set.minvel > 127u)
            set.minvel = 0;
        if (set.octave < -2 || set.octave > 2 || set.bank > NBANK || set.prog >= NPROG)
            settings_defaults();
        if (set.bank == 0)                   /* (0.9.3 and before: no bank loaded) */
            set.bank = (uint8_t)first_bank();
        for (i = 0; i < FXP_N; i++)
            if (set.fx[i] > fx_param(i)->max)
                set.fx[i] = fx_param(i)->def;
        sl_clamp(&set.sl);
        set.sl.p[SL_AON] = 0;                /* (it starts off: the keys play notes at power-on) */
    }
    sl_init(&set.sl);
    fx_init();
    fx_tempo((int32_t)set.sl.p[SL_BPM]);
    fx_set(set.fx);
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
 *   20 B                       bank B (0..7: banks 1..8): erased
 *   21 B P DATA..              program P (0..39) of bank B, 102 bytes as 204 nibbles, low first (as ESQ-1 SysEx)
 *   22 B N                     bank B holds N programs: kept
 *   30 DATA..                  the editor (web/edit): a program (204 nibbles) played now, the notes going on, unsaved
 *   31 B P                     program P of bank B (B 7F: the one playing, as edited): answered
 *                              7E 31 B P E NIBBLES (E 1: edited, not saved)
 *   32 B P DATA..              a program stored into bank B place P (the sector rewritten), then played from there
 * Frames 10..12, 20..22 and 30..32 end with a check: the sum of the bytes from CMD on, mod 16384, as 2 x 7 bits (low first);
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
        return plat_data_erase(bank_base(b)) != 0 ? 2 : 0;
    }
    if (cmd == 0x21u) {
        uint8_t prog[102];
        uint32_t pi = n > 5u ? p[5] : 0xFFu, i;
        if (pi >= NPROG || n < 6u + 204u)
            return 1;
        for (i = 0; i < 102u; i++)
            prog[i] = (uint8_t)((p[6 + 2 * i] & 15u) | (p[7 + 2 * i] & 15u) << 4);
        return bank_write(b, pi, prog) != 0 ? 2 : 0;
    }
    if (cmd == 0x22u) {
        uint8_t h[8] = {0x53, 0x51, 0x42, 0x4B, 0, 0, 0, 0};
        uint32_t cnt = n > 5u ? p[5] : 0u;
        if (cnt == 0 || cnt > NPROG)
            return 1;
        h[4] = (uint8_t)cnt;
        if (plat_data_write(bank_base(b), h, 8) != 0)
            return 2;
        {
            uint32_t x, others = 0;
            for (x = 1; x <= NBANK; x++)
                if (x != b + 1u && bank_stored(x))
                    others = 1;
            if (!others && set.bank != b + 1u) {   /* (the first bank loaded: this one plays) */
                set.bank = (uint8_t)(b + 1u);
                set.prog = 0;
                set_dirty = 1;
            }
            if (set.bank == b + 1u)
                program_load(1);
        }
        return 0;
    }
    return 1;
}

static int program_store(uint32_t bank, uint32_t pi);
static uint8_t redraw_req;                   /* the editor changed the program: the screen shows it */
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
    if ((p[3] >= 0x10u && p[3] <= 0x12u) || (p[3] >= 0x20u && p[3] <= 0x22u) ||
        (p[3] >= 0x30u && p[3] <= 0x32u)) {   /* their last 2 bytes: a check */
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
    case 0x30u:                          /* the editor: a program to play now, the notes going on (unsaved) */
    case 0x32u: {                        /* .. and to store into bank B (0..7), place P (0..39): it plays from there */
        uint32_t i, o = p[3] == 0x30u ? 4u : 6u;
        if (n < o + 204u || (p[3] == 0x32u && (p[4] >= NBANK || p[5] >= NPROG))) {
            reply(p[3], 1);
            return 1;
        }
        for (i = 0; i < 102u; i++)
            prog_raw[i] = (uint8_t)((p[o + 2 * i] & 15u) | (p[o + 1 + 2 * i] & 15u) << 4);
        if (p[3] == 0x32u) {
            if (program_store(p[4] + 1u, p[5]) != 0) {
                reply(0x32, 2);
                return 1;
            }
            set.bank = (uint8_t)(p[4] + 1u);
            set.prog = p[5];
            set_dirty = 1;
            edited = 0;
        } else {
            edited = 1;
        }
        program_play(0);
        redraw_req = 1;
        reply(p[3], 0);
        return 1;
    }
    case 0x31u: {                        /* a program: bank B (0..7) place P, or B = 7F the one playing (as edited):
                                          * 7E 31 B P E NIBBLES (204, low first; E: 1 edited, not saved) */
        uint8_t f[10 + 204], prog[102];
        uint32_t b = n > 4u ? p[4] : 0u, pi = n > 5u ? p[5] : 0u, i;
        if (b == 0x7Fu) {
            for (i = 0; i < 102u; i++)
                prog[i] = prog_raw[i];
            b = set.bank - 1u;
            pi = set.prog;
        } else if (b >= NBANK || pi >= NPROG || prog_read(b + 1u, pi, prog) != 0) {
            reply(0x31, 1);
            return 1;
        }
        f[0] = 0xF0; f[1] = 0x7D; f[2] = 0x53; f[3] = 0x51; f[4] = 0x7E; f[5] = 0x31;
        f[6] = (uint8_t)b; f[7] = (uint8_t)pi; f[8] = (uint8_t)(n > 4u && p[4] == 0x7Fu && edited);
        for (i = 0; i < 102u; i++) {
            f[9 + 2 * i] = prog[i] & 15u;
            f[10 + 2 * i] = prog[i] >> 4;
        }
        f[9 + 204] = 0xF7;
        plat_sysex_send(f, sizeof f);
        return 1;
    }
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
    uint32_t arp_t;                      /* when ARP went down */
    uint8_t arp_down;                    /* ARP held, its hold not acted on yet */
    uint32_t note_keys;                  /* keys that sent a note (their release ends it, whatever the mode) */
    uint8_t step_lock;                   /* SEQ page: the keys are the steps (SEQ tapped there) */
    uint8_t step_page;                   /* steps 1-16, 17-32, 33-48, 49-64 */
    uint8_t seq_used;                    /* SEQ held: a step or a knob used (no tap) */
    uint32_t seq_t;                      /* when SEQ went down */
    uint8_t seq_tap;                     /* .. on its page (a tap there: the keys' mode) */
    uint32_t step_held, step_new, step_turned;   /* step keys down; set by this press; edited by a knob */
    uint32_t rec_t;                      /* when REC went down (0: not down) */
    uint8_t rec_done;                    /* REC held: the pattern cleared */
    uint8_t rec_down;                    /* REC down, not acted on */
    uint8_t edit_used;                   /* EDIT held: undo / redo used (no tap) */
    uint32_t touched;                    /* the last key, button or knob (autosave waits) */
    uint8_t autosave;                    /* a setting kept by itself (CLICK) changed */
    int32_t pos_shown;
    uint32_t state_shown, chg_shown, chg_saved, bpm_sent;
    uint32_t peak;
    uint32_t cpu_shown, voices_shown, peak_shown, loads_shown;
    uint8_t redraw;
    uint8_t view;                        /* HOME's screens: 0 playing (with the scope), 1 the scope alone */
    uint32_t scope_t;                    /* the scope's last frame */
    int32_t scope_peak;                  /* its height: the recent peak */
    uint8_t rnd_amt, rnd_part;           /* RANDOM: how far, which parts */
    uint8_t rnd_n, rnd_seq;              /* .. the programs before (rnd_hist), the next name's number */
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

static void bank_text(char *t)               /* "1".."8", "-" for none */
{
    t[0] = (char)('0' + set.bank);
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
        post(on ? Q_ON : Q_OFF, (uint32_t)CHORD[i], 100u + 128u);   /* (past the arpeggiator) */
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
    for (tries = 0; tries < 2 * (int32_t)NBANK; tries++) {
        int32_t n = (int32_t)bank_count((uint32_t)b);
        if (n && p >= 0 && p < n)
            break;
        if (p < 0) {
            b = b <= 1 ? (int32_t)NBANK : b - 1;
            p = (int32_t)bank_count((uint32_t)b) - 1;
        } else {
            b = b >= (int32_t)NBANK ? 1 : b + 1;
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
    for (tries = 0; tries < (int32_t)NBANK; tries++) {
        b = (b - 1 + d + 2 * (int32_t)NBANK) % (int32_t)NBANK + 1;
        if (bank_count((uint32_t)b))
            break;
    }
    if (b == set.bank || !bank_count((uint32_t)b))
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

/* ------------------------------------------------------------------ editor --- */
/* SEL: the oscillators (again: OSC 1 > 2 > 3), ENV: the envelopes (1..4), LFO: the LFOs (1..3), EDIT: the
 * program's own (filter, output, modes, name); ALGO turns the section's pages, K1..K4 change the page's four
 * values, HOME goes back to the play screen. On the play screen K1..K4 change cutoff, resonance and ENV4's attack
 * and release. SAVE stores the program: PRESET and SELECT choose where (program and bank), SAVE again writes it,
 * HOME leaves it. */
enum { ED_PLAY, ED_OSC, ED_ENV, ED_LFO, ED_PROG, ED_FX, ED_SAVE, ED_SYS, ED_ARP, ED_SEQ, ED_RAND, ED_N };   /* SYS: GLO, the load */
typedef struct {
    const char *title;
    int8_t p[4];                         /* parameter in the group (sq80_params.h), -1 none */
} ed_page;
#define NONE (-1)
static const ed_page OSC_PAGES[] = {
    {"PITCH", {SQ80_P_OSC_OCT, SQ80_P_OSC_SEMI, SQ80_P_OSC_FINE, SQ80_P_OSC_WAVE}},
    {"PITCH MOD", {SQ80_P_OSC_MOD1, SQ80_P_OSC_DEPTH1, SQ80_P_OSC_MOD2, SQ80_P_OSC_DEPTH2}},
    {"DCA", {SQ80_P_OSC_LEVEL, SQ80_P_OSC_OUTPUT, SQ80_P_OSC_DMOD1, SQ80_P_OSC_DDEPTH1}},
    {"DCA MOD 2", {SQ80_P_OSC_DMOD2, SQ80_P_OSC_DDEPTH2, NONE, NONE}}};
static const ed_page ENV_PAGES[] = {
    {"LEVELS", {SQ80_P_ENV_L1, SQ80_P_ENV_L2, SQ80_P_ENV_L3, SQ80_P_ENV_LV}},
    {"TIMES", {SQ80_P_ENV_T1, SQ80_P_ENV_T2, SQ80_P_ENV_T3, SQ80_P_ENV_T4}},
    {"KEY", {SQ80_P_ENV_T1V, SQ80_P_ENV_TK, SQ80_P_ENV_R2, NONE}}};
static const ed_page LFO_PAGES[] = {
    {"WAVE", {SQ80_P_LFO_FREQ, SQ80_P_LFO_WAVE, SQ80_P_LFO_L1, SQ80_P_LFO_L2}},
    {"MORE", {SQ80_P_LFO_DELAY, SQ80_P_LFO_MOD, SQ80_P_LFO_HUMAN, SQ80_P_LFO_RESET}}};
static const ed_page PROG_PAGES[] = {
    {"FILTER", {SQ80_P_FREQ, SQ80_P_RES, SQ80_P_KEYBD, NONE}},
    {"FILTER MOD", {SQ80_P_FMOD1, SQ80_P_FDEPTH1, SQ80_P_FMOD2, SQ80_P_FDEPTH2}},
    {"OUTPUT", {SQ80_P_FINAL, SQ80_P_PAN, SQ80_P_PANMOD, SQ80_P_PANDEPTH}},
    {"MODES", {SQ80_P_SYNC, SQ80_P_AM, SQ80_P_MONO, SQ80_P_GLIDE}},
    {"RESTART", {SQ80_P_VC, SQ80_P_ENVRS, SQ80_P_OSCRS, SQ80_P_CYC}},
    {"NAME 1-4", {SQ80_P_NAME0, SQ80_P_NAME0 + 1, SQ80_P_NAME0 + 2, SQ80_P_NAME0 + 3}},
    {"NAME 5-6", {SQ80_P_NAME0 + 4, SQ80_P_NAME0 + 5, NONE, NONE}}};
/* FX: FMini-1's EFFECTS page and its four pop-ups, as pages (the ids: fmini_fx.h FXP_*) */
static const ed_page FX_PAGES[] = {
    {"LEVELS", {FXP_REV, FXP_DLY, FXP_CHO, FXP_DRIVE}},
    {"REVERB", {FXP_RTYPE, FXP_RSIZE, FXP_RTONE, FXP_REV}},
    {"DELAY", {FXP_DTYPE, FXP_DTIME, FXP_DFDBK, FXP_DLY}},
    {"CHORUS", {FXP_CTYPE, FXP_CDEPTH, FXP_CRATE, FXP_CHO}},
    {"DRIVE", {FXP_DRTYPE, FXP_DRTONE, FXP_DROUT, FXP_DRIVE}}};
static const char *const FX_LEVEL_NAME[4] = {"REVERB", "DELAY", "CHORUS", "DRIVE"};
/* ARP (held) and SEQ: SLOOP's settings (sloop_seq.h SL_*) */
static const ed_page ARP_PAGES[] = {
    {"PATTERN", {SL_AMODE, SL_ARATE, SL_AOCT, SL_AGATE}},
    {"FEEL", {SL_ASWING, SL_AHOLD, SL_AORDER, SL_APROB}},
    {"TEMPO", {SL_BPM, SL_SYNC, SL_AON, NONE}}};
static const ed_page SEQ_PAGES[] = {
    {"STEP", {NONE, NONE, NONE, NONE}},  /* (step entry: K1 the cursor, K2 its note, K3 its time, K4 its flag) */
    {"PATTERN", {SL_SLEN, SL_SDIV, SL_SGATE, SL_SSWING}},
    {"TEMPO", {SL_BPM, SL_SYNC, NONE, NONE}}};
static const char *const STEP_KNOB[4] = {"NOTE", "VELOCITY", "RATCHET", "TYPE"};
static const char *const ENTRY_KNOB[4] = {"STEP", "NOTE", "TIME", "FLAG"};
static const ed_page PLAY_PAGE = {"", {SQ80_P_FREQ, SQ80_P_RES, NONE, NONE}};   /* (K3, K4: ENV4 T1, T4) */

static struct {
    uint8_t sec, inst, page[ED_N];       /* the section, its instance, each section's page */
    uint8_t to_bank, to_prog;            /* SAVE: where to */
    uint8_t back;                        /* SAVE: the section to go back to */
} ed;

static const ed_page *ed_pages(uint32_t sec, uint32_t *n)
{
    switch (sec) {
    case ED_OSC: *n = sizeof OSC_PAGES / sizeof OSC_PAGES[0]; return OSC_PAGES;
    case ED_ENV: *n = sizeof ENV_PAGES / sizeof ENV_PAGES[0]; return ENV_PAGES;
    case ED_LFO: *n = sizeof LFO_PAGES / sizeof LFO_PAGES[0]; return LFO_PAGES;
    case ED_PROG: *n = sizeof PROG_PAGES / sizeof PROG_PAGES[0]; return PROG_PAGES;
    case ED_FX: *n = sizeof FX_PAGES / sizeof FX_PAGES[0]; return FX_PAGES;
    case ED_ARP: *n = sizeof ARP_PAGES / sizeof ARP_PAGES[0]; return ARP_PAGES;
    case ED_SEQ: *n = sizeof SEQ_PAGES / sizeof SEQ_PAGES[0]; return SEQ_PAGES;
    default: *n = 1; return &PLAY_PAGE;
    }
}

static int ed_param(uint32_t k)              /* the parameter id K1..K4 change now, -1 none */
{
    uint32_t n;
    const ed_page *pg = ed_pages(ed.sec, &n) + (ed.sec <= ED_FX || ed.sec >= ED_ARP ? ed.page[ed.sec] % n : 0);
    int q;
    if (ed.sec == ED_PLAY)
        return k == 0 ? SQ80_PARAM_PROG(SQ80_P_FREQ) : k == 1 ? SQ80_PARAM_PROG(SQ80_P_RES) :
               SQ80_PARAM_ENV(3, k == 2 ? SQ80_P_ENV_T1 : SQ80_P_ENV_T4);
    if ((ed.sec > ED_FX && ed.sec < ED_ARP) || (q = pg->p[k]) < 0)
        return -1;
    if (ed.sec == ED_FX || ed.sec >= ED_ARP) /* (an effect's, the arpeggiator's or the sequencer's: not the program's) */
        return q;
    switch (ed.sec) {
    case ED_OSC: return SQ80_PARAM_OSC(ed.inst, q);
    case ED_ENV: return SQ80_PARAM_ENV(ed.inst, q);
    case ED_LFO: return SQ80_PARAM_LFO(ed.inst, q);
    default: return SQ80_PARAM_PROG(q);
    }
}

static void value_text(int id, char *t)      /* a value as the screen shows it */
{
    const sq80_param_info *in = sq80_param(id);
    int v = sq80_param_get(prog_raw, id);
    t[0] = 0;
    switch (in->kind) {
    case SQ80_K_ONOFF: text_cat(t, v ? "ON" : "OFF"); break;
    case SQ80_K_SOURCE: text_cat(t, sq80_source_name(v)); break;
    case SQ80_K_LFOWAVE: text_cat(t, sq80_lfo_wave_name(v)); break;
    case SQ80_K_WAVE: {
        char w[7];
        uint32_t i;
        sq80_rompack_name(&rom, v, w);
        for (i = 6; i > 0 && w[i - 1] == ' '; i--)
            w[i - 1] = 0;
        text_cat(t, w);
        break;
    }
    case SQ80_K_CHAR:
        t[0] = (char)(v == ' ' ? '_' : v);
        t[1] = 0;
        break;
    case SQ80_K_SIGNED:
        if (v > 0)
            text_cat(t, "+");
        int_text(v, t + (v > 0));
        break;
    default: int_text(v, t); break;
    }
}

static void fx_turn(int id, int32_t e)       /* an effect's setting: the instrument's (kept by itself) */
{
    int v = (int)set.fx[id] + e, max = fx_param(id)->max;
    set.fx[id] = (uint8_t)(v < 0 ? 0 : v > max ? max : v);
    fx_set(set.fx);
    set_dirty = 1;
    ui.redraw = 1;
}

static void sl_turn(int id, int32_t e)       /* an arpeggiator or sequencer setting: the instrument's (kept by itself) */
{
    const sl_param_info *in = sl_param((uint32_t)id);
    int v = (int)set.sl.p[id] + e;
    set.sl.p[id] = (uint8_t)(v < in->min ? in->min : v > in->max ? in->max : v);
    set_dirty = 1;
    ui.redraw = 1;
}

static int steps_on(void)                    /* the keys are the steps: SEQ held, or its page locked to them */
{
    return (ui.btn >> B_SEQ & 1u) || (ed.sec == ED_SEQ && ui.step_lock);
}

static void seq_post(uint32_t cmd, uint32_t step, int32_t d)
{
    post(Q_SEQ, cmd, (step & 0xFFu) | ((uint32_t)d & 0xFFu) << 8);
}

static int entry_page(void)                  /* SEQ's STEP page, the keys playing (not the steps): step entry */
{
    return ed.sec == ED_SEQ && ed.page[ED_SEQ] % 3u == 0u && !steps_on();
}

static void entry_turn(uint32_t k, int32_t e)   /* STEP page: K1 the cursor, K2 its notes, K3 its time, K4 its flag */
{
    static const uint8_t CMD[4] = {SC_CURSOR, SC_NOTE, SC_TIME, SC_FLAG};
    if (e > 100)
        e = 100;
    if (e < -100)
        e = -100;
    seq_post(CMD[k], sl_cursor(), e);
    ui.redraw = 1;
}

static void knob_turn(uint32_t k, int32_t e)
{
    int id = ed_param(k);
    const sq80_param_info *in;
    char t[12];
    if (id < 0)
        return;
    if (ed.sec == ED_FX) {
        fx_turn(id, e);
        return;
    }
    if (ed.sec >= ED_ARP) {
        sl_turn(id, e);
        return;
    }
    in = sq80_param(id);
    sq80_param_set(prog_raw, id, sq80_param_get(prog_raw, id) + e);
    edited = 1;
    program_play(0);
    if (ed.sec == ED_PLAY) {                 /* (the editor's pages show it already) */
        value_text(id, t);
        app_say(in->name, t);
    }
    ui.redraw = 1;
}

static void section(uint32_t sec, uint32_t ninst)   /* SEL, ENV, LFO, EDIT: the section, or its next instance */
{
    ui.view = 0;
    if (ed.sec == sec) {
        if (ninst > 1)
            ed.inst = (uint8_t)((ed.inst + 1u) % ninst);
        else {
            uint32_t n;
            ed_pages(sec, &n);
            ed.page[sec] = (uint8_t)((ed.page[sec] + 1u) % n);   /* EDIT again: its next page */
        }
    } else {
        ed.sec = (uint8_t)sec;
        ed.inst = 0;
    }
    ui.msg_until = 0;
    ui.redraw = 1;
}

/* ------------------------------------------------------------------ RANDOM --- */
/* HOME's third screen: new programs from the one playing. K1 AMOUNT how far each value moves (100 %: anywhere in its
 * range; a switch, a source or a wave changes with that chance), K2 PARTS which ones (ALL, OSC, FILTER, ENV, LFO,
 * MOD: the modulation routings), K3 right a new one, left the one before (8 kept), K4 the program as stored. The name,
 * the volume and the voice modes stay; OSC 1 sounds, the filter is not shut, ENV 4 (the amplitude) opens and does
 * not take forever, so a new program is never silent. SAVE stores one as any other */
enum { RP_ALL = SQ80_RP_ALL, RP_N = SQ80_RP_N };   /* (src/sq80/sq80_random.h: the editor page's Randomize too) */
static const char *const RP_NAME[RP_N] = {"ALL", "OSC", "FILTER", "ENV", "LFO", "MOD"};
#define RND_HIST 8u
static uint8_t rnd_hist[RND_HIST][102];
static uint32_t rnd_state = 0x2545F491u;

static void randomize(void)
{
    int i;
    rnd_state ^= plat_ms() * 2654435761u;    /* (a different series each time) */
    if (!rnd_state)
        rnd_state = 1;
    if (ui.rnd_n < RND_HIST)                 /* the one before, for K3 left */
        ui.rnd_n++;
    for (i = (int)RND_HIST - 1; i > 0; i--)
        memcpy(rnd_hist[i], rnd_hist[i - 1], 102);
    memcpy(rnd_hist[0], prog_raw, 102);
    ui.rnd_seq = (uint8_t)(ui.rnd_seq + 1u);
    sq80_randomize(prog_raw, ui.rnd_amt, ui.rnd_part, ui.rnd_seq, &rnd_state);
    edited = 1;
    program_play(0);
}

static void rand_turn(uint32_t k, int32_t e)
{
    if (k == 0u) {
        int v = (int)ui.rnd_amt + (e > 0 ? 5 : -5);
        ui.rnd_amt = (uint8_t)(v < 5 ? 5 : v > 100 ? 100 : v);
    } else if (k == 1u) {
        ui.rnd_part = (uint8_t)((ui.rnd_part + (uint32_t)(e > 0 ? 1 : RP_N - 1)) % RP_N);
    } else if (k == 2u) {
        if (e > 0) {
            randomize();
        } else if (ui.rnd_n) {               /* the one before */
            uint32_t i;
            memcpy(prog_raw, rnd_hist[0], 102);
            for (i = 0; i + 1u < RND_HIST; i++)
                memcpy(rnd_hist[i], rnd_hist[i + 1u], 102);
            ui.rnd_n--;
            edited = 1;
            program_play(0);
        } else {
            app_say("RANDOM", "NONE BEFORE");
        }
    } else {
        program_load(1);                     /* the program as stored */
        ui.rnd_n = 0;
        app_say("PROGRAM", "AS STORED");
    }
    ui.redraw = 1;
}

static uint8_t sector_buf[SECTOR] PLAT_POOL;  /* a bank's sector, rewritten whole to store a program */

static int program_store(uint32_t bank, uint32_t pi)   /* prog_raw into bank (1..8), program pi; 0 done */
{
    uint32_t slot = bank - 1u, n = bank_stored(bank), i, base = bank_base(slot);
    const uint8_t *f = plat_data(base);
    uint8_t init[102];
    if (!f || slot >= NBANK || pi >= NPROG)
        return -1;
    for (i = 0; i < SECTOR; i++)             /* the sector as it is (a bank never loaded: erased) */
        sector_buf[i] = n ? f[i] : 0xFFu;
    sq80_init_program(init);
    if (pi >= n) {                           /* the bank grows to this program: INIT in the places between */
        uint32_t k, j;
        for (k = n; k < pi; k++)
            for (j = 0; j < 102u; j++)
                sector_buf[phys(slot, prog_at(slot, k) + j)] = init[j];
        n = pi + 1u;
    }
    for (i = 0; i < 102u; i++)
        sector_buf[phys(slot, prog_at(slot, pi) + i)] = prog_raw[i];
    sector_buf[0] = 0x53; sector_buf[1] = 0x51; sector_buf[2] = 0x42; sector_buf[3] = 0x4B;   /* "SQBK" */
    sector_buf[4] = (uint8_t)n;
    sector_buf[5] = sector_buf[6] = sector_buf[7] = 0;
    if (plat_data_erase(base) != 0)
        return -2;
    if (slot >= 4u) {                        /* round the gap: it stays erased */
        if (plat_data_write(base, sector_buf, GAP) != 0 ||
            plat_data_write(base + GAP + 8u, sector_buf + GAP + 8u, SECTOR - GAP - 8u) != 0)
            return -3;
    } else if (plat_data_write(base, sector_buf, SECTOR) != 0) {
        return -3;
    }
    return 0;
}

static void save_begin(void)
{
    ed.back = ed.sec == ED_SAVE ? ED_PLAY : ed.sec;
    ed.to_bank = (uint8_t)(set.bank ? set.bank : 1u);
    ed.to_prog = set.prog;
    ed.sec = ED_SAVE;
    ui.msg_until = 0;
    ui.redraw = 1;
}

static void save_commit(void)
{
    char t[24], n[12];
    if (program_store(ed.to_bank, ed.to_prog) != 0) {
        app_say("SAVE", "FAILED");
        ed.sec = ed.back;
        return;
    }
    set.bank = ed.to_bank;
    set.prog = ed.to_prog;
    edited = 0;
    plat_store_save(OBJ_SETTINGS, &set, sizeof set);   /* (and where it is: the next power-on starts there) */
    set_dirty = 0;
    ed.sec = ed.back;
    t[0] = 0;
    bank_text(t);
    text_cat(t, "-");
    if (set.prog + 1u < 10u)
        text_cat(t, "0");
    int_text(set.prog + 1, n);
    text_cat(t, n);
    app_say("SAVED", t);
}

/* the keys, from F3: the white ones are the steps 1-16 of the page, the first four black ones the pages */
static const uint8_t WHITE_KEY[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_KEY[4] = {1, 3, 5, 8};
static int white_of(uint32_t k)
{
    uint32_t w;
    for (w = 0; w < 16u; w++)
        if (WHITE_KEY[w] == k)
            return (int)w;
    return -1;
}
static int black_of(uint32_t k)
{
    uint32_t b;
    for (b = 0; b < 4u; b++)
        if (BLACK_KEY[b] == k)
            return (int)b;
    return -1;
}
static uint32_t seq_len(void) { return set.sl.p[SL_SLEN] ? set.sl.p[SL_SLEN] : 1u; }
static int step_empty(uint32_t idx)
{
    const sl_step *st = &set.sl.step[idx % SL_NSTEP];
    return st->time == SL_ST_REST || (st->time == SL_ST_NOTE && !st->n);
}

static void step_down(uint32_t w)            /* a step key: an empty step is set at once */
{
    uint32_t idx = ui.step_page * 16u + w, bit = 1u << w;
    ui.seq_used = 1;
    if (idx >= seq_len()) {
        app_say("STEP", "PAST THE END");
        return;
    }
    if (!ui.step_held)
        seq_post(SC_MARK, 0, 0);             /* (the steps of one hold: one undo) */
    ui.step_held |= bit;
    ui.step_turned &= ~bit;
    if (step_empty(idx)) {
        seq_post(SC_SET, idx, 0);
        ui.step_new |= bit;
    } else {
        ui.step_new &= ~bit;
    }
}

static void step_up(uint32_t w)              /* a set step pressed and let go (no knob turned): cleared */
{
    uint32_t bit = 1u << w;
    if (!(ui.step_held & bit))
        return;
    if (!((ui.step_new | ui.step_turned) & bit))
        seq_post(SC_CLEAR, ui.step_page * 16u + w, 0);
    ui.step_held &= ~bit;
    ui.step_new &= ~bit;
    ui.step_turned &= ~bit;
}

static void step_turn(uint32_t k, int32_t e) /* a step held: K1 its notes, K2 velocity, K3 ratchets, K4 its type */
{
    static const uint8_t CMD[4] = {SC_NOTE, SC_VEL, SC_RAT, SC_TYPE};
    uint32_t w;
    if (e > 100)
        e = 100;
    if (e < -100)
        e = -100;
    for (w = 0; w < 16u; w++)
        if (ui.step_held >> w & 1u) {
            seq_post(CMD[k], ui.step_page * 16u + w, e);
            ui.step_turned |= 1u << w;
        }
    ui.redraw = 1;
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, now = plat_ms(), prev = ui.btn;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    ui.btn = btn;                            /* (steps_on reads SEQ held from here) */
    for (k = 0; k < NKEYS; k++) {            /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k, v;
        int32_t note = 53 + (int32_t)k + 12 * set.octave;
        if ((keys & bit) && !(ui.keys & bit)) {
            ui.touched = now;
            if (steps_on()) {                /* the steps, the pages */
                int w = white_of(k), b = black_of(k);
                if (w >= 0)
                    step_down((uint32_t)w);
                else if (b >= 0) {
                    ui.step_page = (uint8_t)b;
                    ui.seq_used = 1;
                }
                continue;
            }
            v = touch_vel(k);
            if (v > 127u)
                v = 127u;
            v = set.minvel + v * (127u - set.minvel) / 127u;   /* (GLO MIN VEL: the range above it, as played) */
            if (v < 1u)
                v = 1u;
            post(Q_ON, (uint32_t)note, v);
            ui.note_keys |= bit;
            plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)note << 16 | v << 24, TO_USB | TO_BLE);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            int w = white_of(k);
            ui.touched = now;
            if (ui.note_keys & bit) {
                ui.note_keys &= ~bit;
                post(Q_OFF, (uint32_t)(53 + (int32_t)k + 12 * set.octave), 0);
                plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)(53 + (int32_t)k + 12 * set.octave) << 16, TO_USB | TO_BLE);
            } else if (w >= 0) {
                step_up((uint32_t)w);
            }
        }
    }
    for (k = 0; k < NB; k++) {               /* let go: EDIT, SEQ, REC act on a tap */
        uint32_t bit = 1u << k;
        if ((btn & bit) || !(prev & bit))
            continue;
        ui.touched = now;
        if (k == B_EDIT && !ui.edit_used && ed.sec != ED_SAVE)
            section(ED_PROG, 1);
        else if (k == B_SEQ && !ui.seq_used && ed.sec == ED_SEQ && ui.seq_tap && now - ui.seq_t < 400u) {
            ui.step_lock ^= 1u;              /* SEQ tapped on its page: the keys are the steps, or the notes */
            app_say("KEYS", ui.step_lock ? "STEPS" : "NOTES");
        } else if (k == B_REC) {
            if (ui.rec_down && !ui.rec_done && now - ui.rec_t < 700u)
                seq_post(SC_REC, 0, 0);
            ui.rec_down = 0;
        }
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (prev & bit))
            continue;
        ui.touched = now;
        if (ed.sec == ED_SAVE && k != B_SAVE && k != B_HOME && k != B_OCTDN && k != B_OCTUP)
            continue;                        /* (choosing where to save: SAVE or HOME) */
        if ((k == B_OCTDN || k == B_OCTUP) && (btn >> B_EDIT & 1u) && ed.sec != ED_SAVE) {
            uint32_t st = sl_state();        /* EDIT held: undo / redo (the pattern) */
            ui.edit_used = 1;
            if (k == B_OCTDN ? (st & SL_S_UNDO) : (st & SL_S_REDO)) {
                seq_post(k == B_OCTDN ? SC_UNDO : SC_REDO, 0, 0);
                app_say(k == B_OCTDN ? "UNDO" : "REDO", "");
            } else {
                app_say(k == B_OCTDN ? "UNDO" : "REDO", "NOTHING");
            }
        } else if ((k == B_OCTDN || k == B_OCTUP) && steps_on()) {
            ui.step_page = (uint8_t)((ui.step_page + (k == B_OCTUP ? 1u : 3u)) % 4u);
            ui.seq_used = 1;
            ui.redraw = 1;
        } else if (k == B_OCTDN)
            octave_step(-1);
        else if (k == B_OCTUP)
            octave_step(1);
        else if (k == B_SEL)
            section(ED_OSC, 3);
        else if (k == B_ENV)
            section(ED_ENV, 4);
        else if (k == B_LFO)
            section(ED_LFO, 3);
        else if (k == B_EDIT)
            ui.edit_used = 0;                /* (its page when let go: EDIT + OCT is undo) */
        else if (k == B_FX)
            section(ED_FX, 1);
        else if (k == B_GLO) {               /* the load: CPU now, its peak, under-runs; GLO again: the peak from 0 */
            if (ed.sec == ED_SYS)
                ui.peak = 0;
            ed.sec = ED_SYS;
            ui.msg_until = 0;
            ui.redraw = 1;
        }
        else if (k == B_ARP)
        {
            ui.arp_t = plat_ms();            /* a tap turns it on or off, a hold opens its page (below) */
            ui.arp_down = 1;
        }
        else if (k == B_SEQ) {               /* its page; held, the keys are the steps */
            ui.seq_used = 0;
            ui.seq_t = now;
            ui.seq_tap = ed.sec == ED_SEQ;   /* (a tap there: the keys' mode, when let go) */
            if (ed.sec != ED_SEQ) {
                ed.sec = ED_SEQ;
                ui.step_lock = 0;
                ui.msg_until = 0;
            }
            ui.redraw = 1;
        }
        else if (k == B_PLAY) {
            if (ed.sec == ED_SYS)
                stress_set(!ui.stress);      /* the load test: eight notes held */
            else
                seq_post(SC_PLAY, 0, 0);
        }
        else if (k == B_REC) {
            ui.rec_t = now;                  /* a tap: record / arm; held 2 s: the pattern cleared */
            ui.rec_down = 1;
            ui.rec_done = 0;
        }
        else if (k == B_HOME) {
            if (ed.sec == ED_SAVE) {         /* not saved: back where it was */
                ed.sec = ed.back;
                ui.redraw = 1;
            } else if (ed.sec == ED_PLAY && ui.view == 0u) {   /* HOME's screens: playing, the scope, RANDOM */
                ui.view = 1;
                ui.redraw = 1;
            } else if (ed.sec == ED_PLAY) {
                ui.view = 0;
                ed.sec = ED_RAND;
                ui.msg_until = 0;
                ui.redraw = 1;
            } else {                         /* RANDOM, an editor: back to playing (the edits kept) */
                ed.sec = ED_PLAY;
                ui.view = 0;
                ui.redraw = 1;
            }
        } else if (k == B_SAVE) {
            if (ed.sec == ED_SAVE)
                save_commit();
            else                             /* where to store the program: a bank and a place (the instrument's
                                              * settings, the effects, the pattern keep themselves) */
                save_begin();
        }
    }
    if (ui.rec_down && (btn >> B_REC & 1u) && !ui.rec_done && now - ui.rec_t >= 2000u) {
        ui.rec_done = 1;                     /* REC held 2 s: the pattern cleared (undo brings it back) */
        seq_post(SC_WIPE, 0, 0);
        app_say("PATTERN", "CLEARED");
    }
    if (ui.arp_down) {                       /* ARP: held half a second, its page; let go before, on / off */
        if (btn >> B_ARP & 1u) {
            if (plat_ms() - ui.arp_t >= 500u && ed.sec != ED_SAVE) {
                ui.arp_down = 0;
                ed.sec = ED_ARP;
                ui.msg_until = 0;
                ui.redraw = 1;
            }
        } else {
            ui.arp_down = 0;
            set.sl.p[SL_AON] ^= 1u;
            app_say("ARP", set.sl.p[SL_AON] ? "ON" : "OFF");
        }
    }
    if (ed.sec == ED_SAVE) {                 /* where to: PRESET the program, SELECT the bank */
        int32_t ep = plat_enc(EN_PRESET) + plat_enc(EN_K2), eb = plat_enc(EN_SELECT) + plat_enc(EN_K1);   /* (or K1, K2) */
        if (ep)
            ed.to_prog = (uint8_t)((ed.to_prog + (uint32_t)(ep > 0 ? 1 : (int32_t)NPROG - 1)) % NPROG);
        if (eb)
            ed.to_bank = (uint8_t)((ed.to_bank - 1u + (uint32_t)(eb > 0 ? 1 : (int32_t)NBANK - 1)) % NBANK + 1u);
        (void)plat_enc(EN_ALGO);
        for (k = 2; k < 4u; k++)
            (void)plat_enc((int)(EN_K1 + k));
        if (ep || eb)
            ui.touched = now;
        ui.redraw = 1;
    } else {
        if ((e = plat_enc(EN_PRESET)) != 0)
            program_step(e > 0 ? 1 : -1);
        if ((e = plat_enc(EN_SELECT)) != 0)
            bank_step(e > 0 ? 1 : -1);
        if ((e = plat_enc(EN_ALGO)) != 0 && ed.sec != ED_PLAY) {   /* the section's pages */
            uint32_t n;
            ed_pages(ed.sec, &n);
            ed.page[ed.sec] = (uint8_t)((ed.page[ed.sec] + (uint32_t)(e > 0 ? 1 : (int32_t)n - 1)) % n);
            ui.redraw = 1;
        }
        for (k = 0; k < 4u; k++)
            if ((e = plat_enc((int)(EN_K1 + k))) != 0) {
                ui.touched = now;
                if (steps_on() && ui.step_held) {
                    ui.seq_used = 1;
                    step_turn(k, e);
                } else if (ed.sec == ED_SEQ && ed.page[ED_SEQ] % 3u == 0u) {
                    entry_turn(k, e);
                } else if (ed.sec == ED_RAND) {
                    rand_turn(k, e);
                } else if (ed.sec == ED_SYS) {
                    if (k == 0u) {           /* GLO K1: the metronome (kept by itself, as the pattern) */
                        sl_turn(SL_CLICK, e > 0 ? 1 : -1);
                        ui.autosave = 1;
                    } else if (k == 1u) {    /* K2: the keys' softest velocity */
                        int v = (int)set.minvel + e;
                        set.minvel = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
                        ui.autosave = 1;
                        ui.redraw = 1;
                    }
                } else {
                    knob_turn(k, e);
                }
            }
    }
    if (pc_req != 0xFFFFFFFFu) {             /* MIDI program change: 0..39 in this bank */
        uint32_t pc = pc_req;
        pc_req = 0xFFFFFFFFu;
        if (pc < bank_count(set.bank)) {
            set.prog = (uint8_t)pc;
            program_load(1);
            ui.redraw = 1;
        }
    }
    if (keys != ui.keys || btn != prev)
        ui.redraw = 1;
    ui.keys = keys;
    {
        static const uint8_t LIT[ED_N] = {0, B_SEL, B_ENV, B_LFO, B_EDIT, B_FX, B_SAVE, B_GLO, B_ARP, B_SEQ, B_HOME};
        uint32_t lit = ed.sec != ED_PLAY ? 1u << LIT[ed.sec] : 0u, st = sl_state(), kl = keys, w;
        if (st & SL_S_BEAT)
            lit |= 1u << B_PLAY;             /* PLAY flashes on the beats */
        if ((st & SL_S_REC) || ((st & SL_S_ARMED) && (now / 250u) & 1u))
            lit |= 1u << B_REC;              /* REC: lit recording, blinking armed */
        if (steps_on()) {                    /* the keys: the page's steps, the playhead, the page's black key */
            int32_t pos = sl_pos();
            lit |= 1u << B_SEQ;
            kl = 1u << BLACK_KEY[ui.step_page % 4u];
            for (w = 0; w < 16u; w++) {
                uint32_t idx = ui.step_page * 16u + w, on = idx < seq_len() && !step_empty(idx);
                if ((int32_t)idx == pos)
                    on ^= 1u;
                if (on)
                    kl |= 1u << WHITE_KEY[w];
            }
        }
        {   /* the unlit buttons and keys all glow (plat_leds lets only the buttons glow: the seven lowest keys then
             * caught some of it, the others none), PLAY green */
            uint32_t on = btn | lit | (edited ? 1u << B_SAVE : 0u) | (set.sl.p[SL_AON] ? 1u << B_ARP : 0u) |
                          (ui.stress ? 1u << B_PLAY : 0u), play = 1u << B_PLAY, dimb = ((1u << NB) - 1u) & ~on;
            on = (on & ~play) | (on & play ? LED_PLAY_GREEN : 0u);
            dimb = (dimb & ~play) | (dimb & play ? LED_PLAY_GREEN : 0u);
            plat_lights(on, kl, dimb, ((1u << NKEYS) - 1u) & ~kl, 2);
        }
    }
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

static void knob_cells(int32_t y, const char *const *names)   /* K1..K4: name, value, a bar for numbers */
{
    uint32_t k;
    char t[16];
    cv_begin(240, 46, C_BLACK);
    if (steps_on() && ui.step_held) {        /* a step held: its notes, velocity, ratchets, type */
        uint32_t w;
        const sl_step *st = 0;
        for (w = 0; w < 16u && !st; w++)
            if (ui.step_held >> w & 1u)
                st = &set.sl.step[(ui.step_page * 16u + w) % SL_NSTEP];
        for (k = 0; k < 4u && st; k++) {
            int32_t x = 4 + (int32_t)k * 59;
            t[0] = 0;
            if (k == 0) {
                if (st->time == SL_ST_NOTE && st->n) {
                    note_name(st->note[0], t);
                    if (st->n > 1u)
                        text_cat(t, "+");
                } else {
                    text_cat(t, "-");
                }
            } else if (k == 1) {
                int_text(st->time == SL_ST_NOTE && st->n ? (st->flags & SL_SF_ACCENT ? 127 : st->vel ? st->vel : 96) : 0, t);
            } else if (k == 2) {
                text_cat(t, "X");
                int_text(1 + (st->rat & 3u), t + 1);
            } else {
                text_cat(t, st->time == SL_ST_TIE ? "TIE" : st->time == SL_ST_REST ? "REST" :
                         st->flags & SL_SF_SLIDE ? "SLIDE" : st->flags & SL_SF_ACCENT ? "ACCENT" : "NOTE");
            }
            cv_text(x, 0, &FONT_XS, STEP_KNOB[k], C_GRAY);
            cv_text(x, 14, &FONT_B, t, C_WHITE);
        }
        cv_blit(0, y);
        return;
    }
    if (ed.sec == ED_SEQ && ed.page[ED_SEQ] % 3u == 0u) {   /* STEP: the cursor's step */
        uint32_t c = sl_cursor();
        const sl_step *st = &set.sl.step[c % SL_NSTEP];
        int note = st->time == SL_ST_NOTE && st->n;
        for (k = 0; k < 4u; k++) {
            int32_t x = 4 + (int32_t)k * 59;
            t[0] = 0;
            if (k == 0)
                int_text((int32_t)c + 1, t);
            else if (k == 1) {
                if (note) {
                    note_name(st->note[0], t);
                    if (st->n > 1u)
                        text_cat(t, "+");
                } else {
                    text_cat(t, "-");
                }
            } else if (k == 2)
                text_cat(t, st->time == SL_ST_TIE ? "TIE" : note ? "NOTE" : "REST");
            else
                text_cat(t, !note ? "-" : (st->flags & 3u) == 3u ? "ACC+SLD" : st->flags & SL_SF_SLIDE ? "SLIDE" :
                         st->flags & SL_SF_ACCENT ? "ACCENT" : "-");
            cv_text(x, 0, &FONT_XS, ENTRY_KNOB[k], C_GRAY);
            cv_text(x, 14, &FONT_B, t, C_WHITE);
        }
        cv_blit(0, y);
        return;
    }
    for (k = 0; k < 4u; k++) {
        int32_t x = 4 + (int32_t)k * 59, id = ed_param(k);
        const sq80_param_info *in;
        if (id < 0)
            continue;
        if (ed.sec >= ED_ARP) {              /* an arpeggiator or sequencer setting: name, value, a bar for numbers */
            const sl_param_info *si = sl_param((uint32_t)id);
            int32_t v = set.sl.p[id];
            cv_text(x, 0, &FONT_XS, si->name, C_GRAY);
            cv_text(x, 14, &FONT_B, sl_value_text((uint32_t)id, v, t), C_WHITE);
            if (!si->names) {
                cv_rect(x, 36, 54, 4, C_LINE);
                cv_rect(x, 36, 54 * (v - si->min) / (si->max - si->min), 4, C_ACCENT);
            }
            continue;
        }
        if (ed.sec == ED_FX) {               /* an effect's setting: its name (the levels page: the effect's), value, bar */
            const fx_param_info *fi = fx_param(id);
            int32_t v = set.fx[id];
            cv_text(x, 0, &FONT_XS, ed.page[ED_FX] % 5u == 0 ? FX_LEVEL_NAME[k] : fi->name, C_GRAY);
            cv_text(x, 14, &FONT_B, fx_value_text(id, v, t), C_WHITE);
            if (!fi->names) {
                cv_rect(x, 36, 54, 4, C_LINE);
                cv_rect(x, 36, 54 * v / fi->max, 4, C_ACCENT);
            }
            continue;
        }
        in = sq80_param(id);
        cv_text(x, 0, &FONT_XS, names ? names[k] : in->name, C_GRAY);
        value_text(id, t);
        cv_text(x, 14, &FONT_B, t, C_WHITE);
        if (in->kind == SQ80_K_NUM || in->kind == SQ80_K_SIGNED) {
            int32_t v = sq80_param_get(prog_raw, id), span = in->max - in->min;
            cv_rect(x, 36, 54, 4, C_LINE);
            if (in->min < 0) {               /* signed: from the middle */
                int32_t w = 27 * (v < 0 ? -v : v) / in->max;
                cv_rect(v < 0 ? x + 27 - w : x + 27, 36, w, 4, C_ACCENT);
            } else {
                cv_rect(x, 36, 54 * (v - in->min) / (span ? span : 1), 4, C_ACCENT);
            }
        }
    }
    cv_blit(0, y);
}

/* SEL: the oscillator's wave as the SQ-80 plays it at middle C (its octave and semitone moving the key: the ROM's
 * zones change the sample across the keyboard), drawn from the ROM pack: each column the lowest and highest byte of
 * its share of the table, up to the end or the first zero (where the DOC stops); w x h at x, y of the canvas */
static void wave_draw(int32_t x, int32_t y, int32_t w, int32_t h)
{
    uint32_t o = ed.inst % 3u, n, i, len, c;
    int32_t wave = sq80_param_get(prog_raw, SQ80_PARAM_OSC(o, SQ80_P_OSC_WAVE));
    int32_t key = 60 + 12 * sq80_param_get(prog_raw, SQ80_PARAM_OSC(o, SQ80_P_OSC_OCT)) +
                  sq80_param_get(prog_raw, SQ80_PARAM_OSC(o, SQ80_P_OSC_SEMI));
    sq80_raw_wave rw;
    const uint8_t *src;
    int32_t prev = h / 2, mid = h / 2;
    key = key < 0 ? 0 : key > 127 ? 127 : key;
    rw = sq80_rompack_raw(&rom, rom.zones[wave * SQ80_ZONES + (key >> 3)]);
    n = 1u << rw.size_log2;
    src = rom.rom;
    for (len = 0; len < n && src[(rw.start + len) & rom.rom_mask]; len++)
        ;                                    /* (to its end, or the zero that stops it) */
    cv_rect(x, y + mid, w, 1, C_LINE);
    if (!len)
        return;
    for (c = 0; c < (uint32_t)w; c++) {
        uint32_t a = (uint32_t)((uint64_t)c * len / (uint32_t)w), b = (uint32_t)((uint64_t)(c + 1u) * len / (uint32_t)w);
        int32_t lo = prev, hi = prev;
        if (b <= a)
            b = a + 1u;
        for (i = a; i < b && i < len; i++) {
            int32_t v = (int32_t)src[(rw.start + i) & rom.rom_mask];
            int32_t py = (h - 1) - (v * (h - 1)) / 255;
            if (py < lo)
                lo = py;
            if (py > hi)
                hi = py;
            prev = py;
        }
        cv_rect(x + (int32_t)c, y + lo, 1, hi - lo + 1, C_ACCENT);
    }
}

/* a curve, a y per column (0 at the top .. h - 1), joined column to column; a dim line at y = zero */
static void curve_draw(int32_t x, int32_t y, const int16_t *ys, int32_t w, int32_t zero)
{
    int32_t c, prev = ys[0];
    cv_rect(x, y + zero, w, 1, C_LINE);
    for (c = 0; c < w; c++) {
        int32_t lo = ys[c] < prev ? ys[c] : prev, hi = ys[c] > prev ? ys[c] : prev;
        cv_rect(x + c, y + lo, 1, hi - lo + 1, C_ACCENT);
        prev = ys[c];
    }
}

/* ENV: the envelope's shape: 0 > L1 (T1) > L2 (T2) > L3 (T3), held at L3 while the key is down (a fixed stretch,
 * dim-marked), then back to 0 (T4); each segment as wide as its time (SQ80_ENV_TICKS), at least 3 columns; levels
 * from the middle when any is negative, else from the bottom */
static void env_draw(int32_t x, int32_t y, int32_t w, int32_t h)
{
    int16_t ys[220];
    int32_t e = ed.inst % 4u, lv[5], tk[4], cw[5], k, c = 0, tot = 0, hold = 30, neg, zero, avail;
    lv[0] = 0;
    for (k = 0; k < 3; k++)
        lv[k + 1] = sq80_param_get(prog_raw, SQ80_PARAM_ENV(e, SQ80_P_ENV_L1 + k));
    lv[4] = 0;
    for (k = 0; k < 4; k++) {
        tk[k] = SQ80_ENV_TICKS[sq80_param_get(prog_raw, SQ80_PARAM_ENV(e, SQ80_P_ENV_T1 + k)) & 63];
        tot += tk[k];
    }
    neg = lv[1] < 0 || lv[2] < 0 || lv[3] < 0;
    zero = neg ? h / 2 : h - 1;
    avail = w - hold - 12;
    for (k = 0; k < 4; k++)
        cw[k] = 3 + (int32_t)((int64_t)tk[k] * avail / (tot ? tot : 1));
    if (w > 220)
        w = 220;
    for (k = 0; k < 4 && c < w; k++) {       /* the segments, the hold after the third */
        int32_t i, a = lv[k], b = lv[k + 1];
        for (i = 0; i < cw[k] && c < w; i++, c++) {
            int32_t v = a + (b - a) * (i + 1) / cw[k];
            ys[c] = (int16_t)(neg ? zero - v * (h / 2 - 1) / 63 : zero - v * (h - 1) / 63);
        }
        if (k == 2) {
            int32_t v = lv[3], yy = neg ? zero - v * (h / 2 - 1) / 63 : zero - v * (h - 1) / 63;
            for (i = 0; i < hold && c < w; i++, c++)
                ys[c] = (int16_t)yy;
            cv_rect(x + c, y, 1, h, C_LINE);  /* the key let go */
        }
    }
    for (; c < w; c++)
        ys[c] = (int16_t)zero;
    curve_draw(x, y, ys, w, zero);
}

/* LFO: two seconds of it as the engine runs it (sq80_synth.c lfo_tick: its rate, wave and the DELAY fade from L1
 * to L2), at the control rate; noise from a fixed seed */
static void lfo_draw(int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t n = ed.inst % 3u, freq = sq80_param_get(prog_raw, SQ80_PARAM_LFO(n, SQ80_P_LFO_FREQ));
    int32_t wave = sq80_param_get(prog_raw, SQ80_PARAM_LFO(n, SQ80_P_LFO_WAVE));
    int32_t level = 2 * sq80_param_get(prog_raw, SQ80_PARAM_LFO(n, SQ80_P_LFO_L1));
    int32_t target = 2 * sq80_param_get(prog_raw, SQ80_PARAM_LFO(n, SQ80_P_LFO_L2));
    int32_t delay = sq80_param_get(prog_raw, SQ80_PARAM_LFO(n, SQ80_P_LFO_DELAY)), clock = 0, c, tick, ticks = 167;
    int32_t f = freq << 8, held = 0, zero = h / 2, last = -1;
    uint32_t inc = f < 0x700 ? (uint32_t)(((uint32_t)f << 16) / 7u) : (uint32_t)(f - 0x600) << 16, ph = 0, seed = 1;
    int16_t lo[220], hi[220];
    if (w > 220)
        w = 220;
    for (c = 0; c < w; c++)
        lo[c] = hi[c] = -1;
    for (tick = 0; tick < ticks; tick++) {
        uint32_t before = ph;
        int32_t b, v, yy;
        ph += inc;
        b = (int32_t)(ph >> 24);
        if (wave == 0)
            v = ((b < 0x80 ? b : (~b & 0xff)) - 0x40) * 2;
        else if (wave == 1)
            v = (int8_t)(uint8_t)(b + 0x80);
        else if (wave == 2)
            v = b < 0x80 ? 127 : 0;
        else {
            if (((ph ^ before) & 0x80000000u) || inc >= 0x80000000u) {
                seed = seed * 1664525u + 1013904223u;
                held = (int8_t)(seed >> 24);
            }
            v = held;
        }
        clock -= 1024;
        if (clock <= 0) {
            clock += 7296;
            if (level < target)
                level = level + delay > target ? target : level + delay;
            else if (level > target)
                level = level - delay < target ? target : level - delay;
        }
        yy = zero - (v * level / 126) * (h / 2 - 1) / 127;
        c = tick * w / ticks;
        if (lo[c] < 0 || yy < lo[c])
            lo[c] = (int16_t)yy;
        if (hi[c] < 0 || yy > hi[c])
            hi[c] = (int16_t)yy;
    }
    cv_rect(x, y + zero, w, 1, C_LINE);
    for (c = 0; c < w; c++) {                /* each column: what the ticks in it reach, joined to the last */
        int32_t a = lo[c], b = hi[c];
        if (a < 0)
            a = b = last < 0 ? zero : last;
        if (last >= 0) {
            if (last < a)
                a = last;
            if (last > b)
                b = last;
        }
        cv_rect(x + c, y + a, 1, b - a + 1, C_ACCENT);
        last = hi[c] >= 0 ? (lo[c] + hi[c]) / 2 : last;
    }
}

/* SEQ: the 16 steps of the page as tiles (a note cyan, an accent white, a slide with a tail, a tie joined to the
 * step before, past the length dim), the playhead framed; under them the page, the keys' mode, the transport */
static void seq_draw(void)
{
    uint32_t w, len = seq_len(), st = sl_state();
    int32_t pos = sl_pos();
    char t[32], n[12];
    for (w = 0; w < 16u; w++) {
        uint32_t idx = ui.step_page * 16u + w;
        const sl_step *sp = &set.sl.step[idx % SL_NSTEP];
        int32_t x = 10 + (int32_t)w * 14;
        if (idx >= len) {
            cv_rect(x, 6, 12, 26, RGB(24, 24, 24));
            continue;
        }
        if (sp->time == SL_ST_NOTE && sp->n) {
            cv_rect(x, 6, 12, 26, sp->flags & SL_SF_ACCENT ? C_WHITE : C_ACCENT);
            if (sp->flags & SL_SF_SLIDE)
                cv_rect(x + 12, 16, 2, 6, C_ACCENT);
            if (sp->rat)
                cv_rect(x + 3, 26, 6, 3, C_BLACK);
        } else if (sp->time == SL_ST_TIE) {
            cv_rect(x - 2, 14, 14, 10, C_ACCENT);
        } else {
            cv_rect(x, 6, 12, 26, C_LINE);
            cv_rect(x + 1, 7, 10, 24, C_BLACK);
        }
        if ((int32_t)idx == pos) {
            cv_rect(x, 2, 12, 2, C_WHITE);
            cv_rect(x, 34, 12, 2, C_WHITE);
        }
        if (entry_page() && idx == sl_cursor())   /* step entry: the cursor */
            cv_rect(x, 36, 12, 3, RGB(230, 70, 60));
    }
    t[0] = 0;
    text_cat(t, "PAGE ");
    int_text(ui.step_page + 1, n);
    text_cat(t, n);
    text_cat(t, steps_on() ? "  KEYS: STEPS" : entry_page() ? "  KEYS: STEP ENTRY" : "  KEYS: NOTES");
    cv_text(10, 44, &FONT_XS, t, C_GRAY);
    if (st & (SL_S_PLAY | SL_S_REC | SL_S_ARMED))
        cv_text(230 - text_w(&FONT_XS, st & SL_S_REC ? "REC" : st & SL_S_ARMED ? "READY" : "PLAY"), 44, &FONT_XS,
                st & SL_S_REC ? "REC" : st & SL_S_ARMED ? "READY" : "PLAY",
                st & (SL_S_REC | SL_S_ARMED) ? RGB(230, 70, 60) : C_ACCENT);
}

/* the scope: the last samples from a rising zero crossing (the trace stands still on a steady note), 232 points of
 * two samples each (~10 ms), its height following the recent peak */
#define SCOPE_W 232u
static int16_t scope_pt[SCOPE_W];            /* -1000 .. 1000 */
static void scope_prepare(void)
{
    static int16_t sm[SCOPE_N];
    uint32_t w = scope_w, i, j, need = 2u * SCOPE_W, trig = SCOPE_N - need;
    int32_t pk = 0;
    for (i = 0; i < SCOPE_N; i++)
        sm[i] = scope_cap[(w + i) % SCOPE_N];
    for (j = SCOPE_N - need; j > 1u; j--)
        if (sm[j - 1u] < 0 && sm[j] >= 0) {
            trig = j;
            break;
        }
    for (i = 0; i < need; i++) {
        int32_t v = sm[trig + i] < 0 ? -sm[trig + i] : sm[trig + i];
        if (v > pk)
            pk = v;
    }
    ui.scope_peak = pk > ui.scope_peak ? pk : ui.scope_peak - (ui.scope_peak - pk) / 8;
    if (ui.scope_peak < 600)
        ui.scope_peak = 600;                 /* (quiet: a flat line, not the noise blown up) */
    for (i = 0; i < SCOPE_W; i++) {
        int32_t v = ((int32_t)sm[trig + 2u * i] + sm[trig + 2u * i + 1u]) / 2 * 1000 / ui.scope_peak;
        scope_pt[i] = (int16_t)(v > 1000 ? 1000 : v < -1000 ? -1000 : v);
    }
}
/* the trace into the canvas whose row 0 is screen row top, over screen rows y .. y + h */
static void scope_paint(int32_t top, int32_t y, int32_t h)
{
    int32_t cy = y + h / 2 - top, half = h / 2 - 2, x, py = cy;
    cv_rect(4, cy, (int32_t)SCOPE_W, 1, C_LINE);
    for (x = 0; x < (int32_t)SCOPE_W; x++) {
        int32_t yy = cy - scope_pt[x] * half / 1000;
        if (x)
            cv_line(3 + x, py, 4 + x, yy, C_ACCENT);
        py = yy;
    }
}
static void scope_show(void)                 /* the scope where this screen has it */
{
    scope_prepare();
    if (ed.sec == ED_PLAY && ui.view == 1u) {   /* alone: the whole screen, the program's name small at the foot */
        char t[24];
        cv_begin(240, 120, C_BLACK);
        scope_paint(0, 6, 206);
        cv_blit(0, 0);
        cv_begin(240, 120, C_BLACK);
        scope_paint(120, 6, 206);
        t[0] = 0;
        bank_text(t);
        text_cat(t, "-");
        int_text(set.prog + 1, t + 2);
        text_cat(t, "  ");
        text_cat(t, prog_name);
        cv_text(120 - text_w(&FONT_XS, t) / 2, 100, &FONT_XS, t, edited ? C_ACCENT : C_GRAY);
        cv_blit(0, 120);
    } else if (ed.sec == ED_PLAY) {
        cv_begin(240, 52, C_BLACK);
        scope_paint(0, 2, 48);
        cv_blit(0, 146);
    } else if (ed.sec == ED_RAND) {
        cv_begin(240, 66, C_BLACK);
        scope_paint(0, 4, 58);
        cv_blit(0, 174);
    }
}

static void draw(void)
{
    static const char *const PLAY_NAMES[4] = {"CUTOFF", "RESON", "ATTACK", "RELEASE"};
    static const char *const SEC_NAME[] = {"", "OSC", "ENV", "LFO", "PROGRAM", "FX", "", "", "ARP", "SEQ"};
    char t[32], n[12];
    uint32_t now = plat_ms();
    static uint8_t was_alone;
    if (ed.sec == ED_PLAY && ui.view == 1u) {   /* HOME's second screen: the scope alone */
        if (!was_alone)
            lcd_fill(0, 0, 240, 240, C_BLACK);
        was_alone = 1;
        scope_show();
        return;
    }
    if (was_alone) {                         /* (from the scope alone: nothing of it stays) */
        was_alone = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
    }
    /* the header: the name, or a message for a moment */
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(ui.msg[0][0] ? x + 8 : 10, 6, &FONT_B, ui.msg[1], C_ACCENT);
    } else {
        cv_text(10, 6, &FONT_B, APP_NAME, C_WHITE);
        if (ed.sec != ED_PLAY)
            cv_text(text_w(&FONT_B, APP_NAME) + 18, 6, &FONT_B, ed.sec == ED_SAVE ? "SAVE" : ed.sec == ED_FX ? "FX" :
                    ed.sec == ED_SYS ? "SYSTEM" : ed.sec == ED_ARP ? "ARP" : ed.sec == ED_SEQ ? "SEQ" :
                    ed.sec == ED_RAND ? "RANDOM" : "EDIT", C_ACCENT);
        if (last_note >= 0) {
            note_name(last_note, t);
            cv_text(230 - text_w(&FONT_B, t), 6, &FONT_B, t, C_ACCENT);
        }
    }
    cv_blit(0, 0);
    /* the program: bank, number, name (cyan while changed and not saved) */
    cv_begin(240, 64, C_BLACK);
    cv_text(10, 2, &FONT_XS, "BANK", C_GRAY);
    bank_text(t);
    cv_text(44, 2, &FONT_XS, t, C_ACCENT);
    cv_text(70, 2, &FONT_XS, "PROGRAM", C_GRAY);
    int_text(set.prog + 1, t);
    text_cat(t, " / ");
    int_text((int32_t)bank_count(set.bank), n);
    text_cat(t, n);
    cv_text(124, 2, &FONT_XS, t, C_ACCENT);
    if (edited)
        cv_text(196, 2, &FONT_XS, "EDITED", C_ACCENT);
    {                                        /* the name in 14-segment letters (fonts/seg_font.h: DSEG14) */
        char nm[8];
        uint32_t i;
        for (i = 0; i < 7u && prog_name[i]; i++)
            nm[i] = prog_name[i] >= 'a' && prog_name[i] <= 'z' ? (char)(prog_name[i] - 32) : prog_name[i];
        nm[i] = 0;
        cv_text(10, 22, &FONT_SEG, nm, edited ? C_ACCENT : C_WHITE);
    }
    cv_blit(0, 34);
    if (ed.sec == ED_SAVE) {                 /* where to save */
        uint8_t there[102];
        cv_begin(240, 140, C_BLACK);
        cv_text(10, 4, &FONT_XS, "SAVE TO", C_GRAY);
        t[0] = 0;
        text_cat(t, "BANK ");
        int_text(ed.to_bank, n);
        text_cat(t, n);
        text_cat(t, "  PROGRAM ");
        int_text(ed.to_prog + 1, n);
        text_cat(t, n);
        cv_text(10, 20, &FONT_B, t, C_ACCENT);
        if (prog_read(ed.to_bank, ed.to_prog, there) == 0) {
            uint32_t i;
            for (i = 0; i < 6u; i++)
                n[i] = there[i] >= 32 && there[i] < 127 ? (char)there[i] : ' ';
            n[6] = 0;
            cv_text(10, 44, &FONT_XS, "REPLACES", C_GRAY);
            cv_text(76, 44, &FONT_XS, n, C_WHITE);
        } else {
            cv_text(10, 44, &FONT_XS, "AN EMPTY PLACE", C_GRAY);
        }
        cv_text(10, 84, &FONT_XS, "SELECT / K1: BANK   PRESET / K2: PROGRAM", C_DIM);
        cv_text(10, 104, &FONT_XS, "SAVE: WRITE IT   HOME: CANCEL", C_DIM);
        cv_blit(0, 100);
        return;
    }
    if (ed.sec == ED_SYS) {                  /* GLO: the audio load (as SQ-80 TEST showed it) */
        uint32_t cpu = plat_cpu_pct(), x = plat_xruns();
        cv_begin(240, 140, C_BLACK);
        cv_text(10, 2, &FONT_B, "SYSTEM", C_ACCENT);
        cv_text(10, 30, &FONT_XS, "CPU", C_GRAY);
        int_text((int32_t)cpu, t);
        text_cat(t, "%");
        cv_text(70, 26, &FONT_B, t, cpu >= 85u ? RGB(230, 70, 60) : C_WHITE);
        cv_text(130, 30, &FONT_XS, "PEAK", C_GRAY);
        int_text((int32_t)ui.peak, t);
        text_cat(t, "%");
        cv_text(170, 26, &FONT_B, t, ui.peak >= 85u ? RGB(230, 70, 60) : C_ACCENT);
        cv_rect(10, 50, 220, 6, C_LINE);
        cv_rect(10, 50, 220 * (int32_t)(cpu > 100u ? 100u : cpu) / 100, 6, C_ACCENT);
        if (ui.peak > 1u && ui.peak <= 100u)
            cv_rect(10 + 220 * (int32_t)ui.peak / 100 - 2, 48, 2, 10, C_WHITE);
        cv_text(10, 70, &FONT_XS, "XRUN", C_GRAY);
        int_text((int32_t)x, t);
        cv_text(70, 70, &FONT_XS, t, x ? RGB(230, 70, 60) : C_WHITE);
        cv_text(130, 70, &FONT_XS, "VOICES", C_GRAY);
        int_text((int32_t)voices_now, t);
        cv_text(190, 70, &FONT_XS, t, C_WHITE);
        cv_text(10, 88, &FONT_XS, "CORES", C_GRAY);
        cv_text(70, 88, &FONT_XS, two_cores && plat_cpu2_on() ? "2" : "1", C_WHITE);
        cv_text(130, 88, &FONT_XS, "CLICK", C_GRAY);
        cv_text(190, 88, &FONT_XS, sl_value_text(SL_CLICK, set.sl.p[SL_CLICK], t), C_ACCENT);
        cv_text(10, 104, &FONT_XS, "MIN VEL", C_GRAY);
        int_text(set.minvel, t);
        cv_text(70, 104, &FONT_XS, set.minvel ? t : "OFF", C_ACCENT);
        cv_text(10, 122, &FONT_XS, "K1 CLICK  K2 MIN VEL  PLAY: 8 VOICES", C_DIM);
        cv_blit(0, 100);
        return;
    }
    if (ed.sec == ED_RAND) {                 /* HOME's third screen: new programs at random */
        static const char *const RK[4] = {"AMOUNT", "PARTS", "NEW", "STORED"};
        uint32_t k;
        cv_begin(240, 28, C_BLACK);
        cv_text(10, 2, &FONT_B, "RANDOM", C_ACCENT);
        cv_text(20 + text_w(&FONT_B, "RANDOM"), 2, &FONT_B, "NEW PROGRAMS", C_WHITE);
        cv_blit(0, 100);
        cv_begin(240, 46, C_BLACK);
        for (k = 0; k < 4u; k++) {
            int32_t x = 4 + (int32_t)k * 59;
            t[0] = 0;
            if (k == 0) {
                int_text(ui.rnd_amt, t);
                text_cat(t, "%");
                cv_rect(x, 36, 54, 4, C_LINE);
                cv_rect(x, 36, 54 * ui.rnd_amt / 100, 4, C_ACCENT);
            } else if (k == 1) {
                text_cat(t, RP_NAME[ui.rnd_part % RP_N]);
            } else if (k == 2) {
                text_cat(t, "< >");
            } else {
                text_cat(t, "BACK");
            }
            cv_text(x, 0, &FONT_XS, RK[k], C_GRAY);
            cv_text(x, 14, &FONT_B, t, C_WHITE);
        }
        cv_blit(0, 128);
        scope_show();
        return;
    }
    if (ed.sec != ED_PLAY) {                 /* an editor page */
        uint32_t np;
        ed_pages(ed.sec, &np);
        cv_begin(240, 28, C_BLACK);            /* (all of it from the program block down: nothing of the play screen
                                                * stays) */
        t[0] = 0;
        text_cat(t, SEC_NAME[ed.sec]);
        if (ed.sec != ED_PROG && ed.sec != ED_FX && ed.sec < ED_ARP) {
            text_cat(t, " ");
            int_text(ed.inst + 1, n);
            text_cat(t, n);
        }
        cv_text(10, 2, &FONT_B, t, C_ACCENT);
        cv_text(20 + text_w(&FONT_B, t), 2, &FONT_B, ed_pages(ed.sec, &np)[ed.page[ed.sec] % np].title, C_WHITE);
        int_text(ed.page[ed.sec] % np + 1, t);
        text_cat(t, "/");
        int_text((int32_t)np, n);
        text_cat(t, n);
        cv_text(230 - text_w(&FONT_XS, t), 6, &FONT_XS, t, C_GRAY);
        cv_blit(0, 100);
        knob_cells(128, 0);
        cv_begin(240, 66, C_BLACK);
        if (ed.sec == ED_OSC || ed.sec == ED_ENV || ed.sec == ED_LFO) {   /* a picture in place of the help */
            if (ed.sec == ED_OSC)
                wave_draw(10, 8, 220, 52);
            else if (ed.sec == ED_ENV)
                env_draw(10, 8, 220, 52);
            else
                lfo_draw(10, 8, 220, 52);
            cv_blit(0, 174);
            return;
        }
        if (ed.sec == ED_SEQ) {              /* the page's 16 steps, the playhead, where the keys go */
            seq_draw();
            cv_blit(0, 174);
            return;
        }
        cv_text(10, 20, &FONT_XS, "ALGO: PAGE   HOME: PLAY", C_DIM);
        cv_text(10, 40, &FONT_XS, ed.sec == ED_FX || ed.sec == ED_ARP ? "KEPT BY ITSELF   SAVE: THE PROGRAM" :
                "SAVE: STORE THE PROGRAM", C_DIM);
        cv_blit(0, 174);
        return;
    }
    knob_cells(100, PLAY_NAMES);
    scope_show();                            /* the output, under the knobs */
    /* the octave, MIDI in; the transport, a load under way, or no ROM pack yet */
    cv_begin(240, 42, C_BLACK);
    cv_text(10, 4, &FONT_XS, "OCT", C_GRAY);
    t[0] = set.octave < 0 ? '-' : '+';
    t[1] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
    t[2] = 0;
    cv_text(44, 4, &FONT_XS, t, C_ACCENT);
    if (midi_count) {
        static const char *const PORT[NPORTS] = {"USB", "BLE", "TRS"};
        cv_text(120, 4, &FONT_XS, "MIDI", C_GRAY);
        cv_text(156, 4, &FONT_XS, midi_last_port < NPORTS ? PORT[midi_last_port] : "", C_ACCENT);
    }
    {
        uint32_t st = sl_state();
        if (ld.active) {
            int_text((int32_t)(ld.got * 100u / (ld.size ? ld.size : 1u)), t);
            text_cat(t, "%");
            cv_text(10, 22, &FONT_XS, "LOADING ROM PACK", C_ACCENT);
            cv_text(140, 22, &FONT_XS, t, C_WHITE);
        } else if (st & (SL_S_PLAY | SL_S_REC | SL_S_ARMED)) {   /* the transport */
            t[0] = 0;
            text_cat(t, st & SL_S_REC ? "REC" : st & SL_S_ARMED ? "REC READY" : "PLAY");
            cv_text(10, 22, &FONT_XS, t, st & (SL_S_REC | SL_S_ARMED) ? RGB(230, 70, 60) : C_ACCENT);
            int_text(set.sl.p[SL_BPM], t);
            text_cat(t, st & SL_S_SYNC ? " BPM MIDI" : " BPM");
            cv_text(100, 22, &FONT_XS, t, C_WHITE);
            if (sl_pos() >= 0) {
                int_text(sl_pos() + 1, t);
                text_cat(t, "/");
                int_text((int32_t)seq_len(), n);
                text_cat(t, n);
                cv_text(230 - text_w(&FONT_XS, t), 22, &FONT_XS, t, C_GRAY);
            }
        } else if (!rom_real) {
            cv_text(10, 22, &FONT_XS, "BUILT-IN WAVES   HOME: SCOPE, RANDOM", C_GRAY);
        } else {
            cv_text(10, 22, &FONT_XS, "HOME: SCOPE, RANDOM", C_DIM);
        }
    }
    cv_blit(0, 198);
}

/* the boot screen: SQ-80 in block letters of our own (6 x 9 cells, strokes two cells thick, the last row only
 * Q's tail), cream on black, the hyphen and a rule under the word in cyan */
#define LOGO_CELL 5
#define LOGO_GAP 7
#define LOGO_W 6
#define LOGO_H 9
#define LOGO_CREAM RGB(242, 234, 214)
#define LOGO_CYAN C_ACCENT
static const uint8_t LOGO_GLYPH[4][LOGO_H] = {   /* a row a byte, bit 5 the left cell */
    {0x3F, 0x3F, 0x30, 0x3F, 0x3F, 0x03, 0x3F, 0x3F, 0x00},   /* S */
    {0x3F, 0x3F, 0x33, 0x33, 0x33, 0x37, 0x3F, 0x3F, 0x03},   /* Q, its tail below */
    {0x3F, 0x3F, 0x33, 0x3F, 0x3F, 0x33, 0x3F, 0x3F, 0x00},   /* 8 */
    {0x3F, 0x3F, 0x33, 0x37, 0x3B, 0x33, 0x3F, 0x3F, 0x00},   /* 0, slashed */
};
static void logo_glyph(int32_t x, int32_t y, const uint8_t *g)
{
    int32_t r, c;
    for (r = 0; r < LOGO_H; r++)
        for (c = 0; c < LOGO_W; c++)
            if (g[r] >> (LOGO_W - 1 - c) & 1u) {   /* a run of cells in one rectangle */
                int32_t e = c;
                while (e + 1 < LOGO_W && (g[r] >> (LOGO_W - 2 - e) & 1u))
                    e++;
                cv_rect(x + c * LOGO_CELL, y + r * LOGO_CELL, (e - c + 1) * LOGO_CELL, LOGO_CELL, LOGO_CREAM);
                c = e;
            }
}

void app_splash(void)                        /* at boot, before app_ui_init */
{
    const int32_t gw = LOGO_W * LOGO_CELL, hw = 3 * LOGO_CELL;
    const int32_t total = 4 * gw + hw + 4 * LOGO_GAP, x0 = (240 - total) / 2, y = 4;
    int32_t x = x0, w;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(240, 100, C_BLACK);
    logo_glyph(x, y, LOGO_GLYPH[0]);
    x += gw + LOGO_GAP;
    logo_glyph(x, y, LOGO_GLYPH[1]);
    x += gw + LOGO_GAP;
    cv_rect(x, y + 3 * LOGO_CELL + LOGO_CELL / 2, hw, 2 * LOGO_CELL, LOGO_CYAN);   /* the hyphen */
    x += hw + LOGO_GAP;
    logo_glyph(x, y, LOGO_GLYPH[2]);
    x += gw + LOGO_GAP;
    logo_glyph(x, y, LOGO_GLYPH[3]);
    cv_rect(x0 + 2 * gw + LOGO_GAP + 4, y + 8 * LOGO_CELL + 2, total - 2 * gw - LOGO_GAP - 4, 3, LOGO_CYAN);   /* the rule,
                                                                                    * from Q's tail on */
    w = text_w(&FONT_XS, "FOR THE FM-1");
    cv_text((240 - w) / 2, y + LOGO_H * LOGO_CELL + 16, &FONT_XS, "FOR THE FM-1", C_GRAY);
    cv_blit(0, 60);
}

void app_ui_init(void)
{
    ui.rnd_amt = 30;
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
        (loads != ui.loads_shown || (ed.sec == ED_SYS && (cpu != ui.cpu_shown || ui.peak != ui.peak_shown ||
                                                          v != ui.voices_shown)))) {
        last_tick = now;
        ui.cpu_shown = cpu;
        ui.voices_shown = v;
        ui.peak_shown = ui.peak;
        ui.loads_shown = loads;
        ui.redraw = 1;
    }
    if (redraw_req) {
        redraw_req = 0;
        ui.redraw = 1;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    sl_entry(entry_page());                  /* (STEP page: the notes played write the cursor's step) */
    if (entry_page() && ui.step_page != sl_cursor() / 16u) {
        ui.step_page = (uint8_t)(sl_cursor() / 16u);   /* (the page shown follows the cursor) */
        ui.redraw = 1;
    }
    {   /* the sequencer: the screen follows its steps; the delay its tempo; a changed pattern kept once it rests */
        uint32_t st = sl_state() & ~(uint32_t)SL_S_BEAT, chg = sl_changes();
        int32_t pos = sl_pos();
        if (st != ui.state_shown || chg != ui.chg_shown ||
            (pos != ui.pos_shown && (ed.sec == ED_SEQ || ed.sec == ED_PLAY))) {
            ui.state_shown = st;
            ui.chg_shown = chg;
            ui.pos_shown = pos;
            ui.redraw = 1;
        }
        if (set.sl.p[SL_BPM] != ui.bpm_sent) {
            ui.bpm_sent = set.sl.p[SL_BPM];
            fx_tempo((int32_t)ui.bpm_sent);
        }
        if ((chg != ui.chg_saved || ui.autosave || set_dirty) && !(st & (SL_S_PLAY | SL_S_REC)) && !ui.step_held &&
            ed.sec != ED_SAVE && now - ui.touched >= 3000u) {   /* (and the settings: bank, program, octave, effects) */
            ui.chg_saved = chg;              /* (stopped, nothing touched for 3 s: the pattern to flash) */
            ui.autosave = 0;
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0)
                set_dirty = 0;
        }
    }
    if (ui.redraw) {
        ui.redraw = 0;
        ui.scope_t = now;
        draw();
    } else if ((ed.sec == ED_PLAY || ed.sec == ED_RAND) && now - ui.scope_t >= (ui.view ? 60u : 40u)) {
        ui.scope_t = now;                    /* the scope moves: ~25 frames a second (alone, the whole screen: ~16) */
        scope_show();
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
        *got = (int)bank_stored(set.bank);
    else if (!strcmp(what, "rom"))
        *got = rom_real;
    else if (!strcmp(what, "voices"))
        *got = (int)voices_now;
    else if (!strcmp(what, "note"))
        *got = last_note;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strncmp(what, "raw", 3))       /* rawN: byte N of the playing program */
        *got = prog_raw[atoi(what + 3) % 102];
    else if (!strncmp(what, "fx", 2))        /* fxN: effect setting N (fmini_fx.h FXP_*) */
        *got = set.fx[atoi(what + 2) % FXP_N];
    else if (!strcmp(what, "uib"))           /* the buttons at the last input */
        *got = (int)ui.btn;
    else if (!strcmp(what, "holdt"))         /* ARP held, its hold not acted on yet */
        *got = ui.arp_down;
    else if (!strncmp(what, "sl", 2) && what[2] >= '0' && what[2] <= '9')   /* slN: SLOOP setting N (SL_*) */
        *got = set.sl.p[atoi(what + 2) % SL_NP];
    else if (!strcmp(what, "seqstate"))      /* SL_S_*, the beat left out */
        *got = (int)(sl_state() & ~(uint32_t)SL_S_BEAT & ~(uint32_t)SL_S_UNDO & ~(uint32_t)SL_S_REDO);
    else if (!strcmp(what, "seqpos"))
        *got = sl_pos();
    else if (!strcmp(what, "minvel"))
        *got = set.minvel;
    else if (!strcmp(what, "view"))
        *got = ui.view;
    else if (!strcmp(what, "cursor"))
        *got = (int)sl_cursor();
    else if (!strcmp(what, "steppage"))
        *got = ui.step_page;
    else if (!strncmp(what, "stepn", 5))     /* stepnN: how many notes step N holds (-1 a tie) */
        *got = set.sl.step[atoi(what + 5) % SL_NSTEP].time == SL_ST_TIE ? -1 :
               set.sl.step[atoi(what + 5) % SL_NSTEP].time == SL_ST_REST ? 0 : set.sl.step[atoi(what + 5) % SL_NSTEP].n;
    else if (!strncmp(what, "step", 4))      /* stepN: step N's first note (0: none) */
        *got = set.sl.step[atoi(what + 4) % SL_NSTEP].time == SL_ST_NOTE ? set.sl.step[atoi(what + 4) % SL_NSTEP].note[0] : 0;

    else if (!strcmp(what, "sec"))
        *got = ed.sec;
    else if (!strcmp(what, "inst"))
        *got = ed.inst;
    else if (!strcmp(what, "page"))
        *got = ed.page[ed.sec < ED_N ? ed.sec : 0];
    else if (!strcmp(what, "edited"))
        *got = edited;
    else if (!strcmp(what, "cutoff"))
        *got = play_patch[play_ix].cutoff;
    else if (!strcmp(what, "name0"))         /* the program name's first letter */
        *got = prog_name[0];
    else if (!strcmp(what, "cores"))
        *got = two_cores && plat_cpu2_on() ? 2 : 1;
    else if (!strcmp(what, "stress"))
        *got = ui.stress;
    else if (!strcmp(what, "splash")) {      /* (the simulator shows no splash: drawn on request, for a shot) */
        app_splash();
        *got = 1;
    }
    else
        return 0;
    return 1;
}
#endif
