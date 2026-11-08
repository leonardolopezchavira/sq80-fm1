/* SPDX-License-Identifier: MIT
 * SQ-80 TEST: the sq80 engine on the FM-1 (fm1-base app; started from fm1-base's template, MPL-2.0).
 *
 * The keys play the engine with velocity; MIDI in (USB, Bluetooth, TRS) plays it too, with the wheel, sustain,
 * pitch bend and pressure. PRESET picks one of four test sounds, OCT- / OCT+ the octave, SAVE keeps both.
 * ARP is the stress test: it holds an eight-note chord on every voice, the engine's worst case. The screen
 * shows the audio CPU load now and its peak since the last reset (HOME), and the audio under-runs.
 *
 * The waves are a synthetic test set built in RAM (sq80_test_pack): a sine, a saw, a square and a decaying
 * one-shot. No Ensoniq data is in this firmware.
 *
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in
 * scope. The engine is compiled apart at -O2 (fm1.toml o2). */
#include "sq80.h"

#define SET_MAGIC 0x53513854u            /* "SQ8T" */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t patch;                       /* 0 .. NPATCH-1 */
    uint8_t rsv[2];
} settings_t;
static settings_t set;
static uint8_t set_dirty;

#define NPATCH 4
static const char *const PATCH_NAME[NPATCH] = {"SAW PAD", "SQR BASS", "SINE LEAD", "ONE-SHOT"};

/* ------------------------------------------------------------------ engine --- */
static uint8_t pack_buf[SQ80_TEST_PACK_SIZE];
static sq80_rompack rom;
static sq80_synth syn;
static sq80_patch patches[NPATCH];
static float buf_l[APP_BLOCK], buf_r[APP_BLOCK];
static float gain_now;

static void make_patches(void)
{
    sq80_patch *p;
    int i;
    for (i = 0; i < NPATCH; i++)
        sq80_default_patch(&patches[i]);

    /* SAW PAD: two saws a little apart, the filter opened by ENV2, slow attack and release */
    p = &patches[0];
    p->osc[0].wave = 0;
    p->osc[1].wave = 0;
    p->osc[1].fine = 6;
    p->osc[1].level = 60;
    p->osc[1].output = 1;
    p->cutoff = 60;
    p->resonance = 6;
    p->key_track = 32;
    p->env[1].l1 = 63; p->env[1].l2 = 40; p->env[1].l3 = 30;
    p->env[1].t1 = 40; p->env[1].t2 = 40; p->env[1].t3 = 40; p->env[1].t4 = 35;
    p->filter_mod[0].source = SQ80_ENV2;
    p->filter_mod[0].depth = 30;
    p->env[3].l1 = 63; p->env[3].l2 = 63; p->env[3].l3 = 63;
    p->env[3].t1 = 30; p->env[3].t2 = 20; p->env[3].t3 = 20; p->env[3].t4 = 38;
    p->env[3].lv = 20;
    p->lfo[0].freq = 18; p->lfo[0].wave = 0; p->lfo[0].l1 = 8; p->lfo[0].l2 = 8;
    p->osc[0].pitch_mod[0].source = SQ80_LFO1;
    p->osc[0].pitch_mod[0].depth = 3;
    p->pan = 6;

    /* SQR BASS: a square an octave down, a resonant filter plucked by ENV2 */
    p = &patches[1];
    p->osc[0].wave = 3;
    p->osc[0].octave = -1;
    p->osc[1].wave = 0;
    p->osc[1].octave = -1;
    p->osc[1].level = 50;
    p->osc[1].output = 1;
    p->cutoff = 30;
    p->resonance = 20;
    p->key_track = 40;
    p->env[1].l1 = 63; p->env[1].l2 = 10; p->env[1].l3 = 0;
    p->env[1].t1 = 0; p->env[1].t2 = 25; p->env[1].t3 = 30; p->env[1].t4 = 20;
    p->env[1].lv = 30;
    p->filter_mod[0].source = SQ80_ENV2;
    p->filter_mod[0].depth = 55;
    p->env[3].l1 = 63; p->env[3].l2 = 55; p->env[3].l3 = 50;
    p->env[3].t1 = 0; p->env[3].t2 = 30; p->env[3].t3 = 30; p->env[3].t4 = 15;
    p->env[3].lv = 25;

    /* SINE LEAD: a sine with a fifth above, delayed vibrato and the wheel on LFO1 */
    p = &patches[2];
    p->osc[0].wave = 2;
    p->osc[1].wave = 2;
    p->osc[1].semi = 7;
    p->osc[1].level = 40;
    p->osc[1].output = 1;
    p->osc[2].wave = 0;
    p->osc[2].octave = 1;
    p->osc[2].level = 24;
    p->osc[2].output = 1;
    p->cutoff = 90;
    p->resonance = 4;
    p->lfo[0].freq = 22; p->lfo[0].wave = 0; p->lfo[0].l1 = 0; p->lfo[0].l2 = 20; p->lfo[0].delay = 30;
    p->lfo[0].mod = SQ80_WHEEL;
    for (i = 0; i < 3; i++) {
        p->osc[i].pitch_mod[0].source = SQ80_LFO1;
        p->osc[i].pitch_mod[0].depth = 6;
    }
    p->env[3].l1 = 63; p->env[3].l2 = 63; p->env[3].l3 = 63;
    p->env[3].t1 = 5; p->env[3].t2 = 10; p->env[3].t3 = 10; p->env[3].t4 = 25;
    p->glide = 20;

    /* ONE-SHOT: the decaying one-shot wave, as the SQ-80's transwaves and drums play (waves 54 and up) */
    p = &patches[3];
    p->osc[0].wave = 60;
    p->osc[1].wave = 2;
    p->osc[1].octave = 2;
    p->osc[1].level = 30;
    p->osc[1].output = 1;
    p->cutoff = 110;
    p->env[3].l1 = 63; p->env[3].l2 = 0; p->env[3].l3 = 0;
    p->env[3].t1 = 0; p->env[3].t2 = 45; p->env[3].t3 = 30; p->env[3].t4 = 30;
    p->env[3].lv = 40;
}

/* the main loop's events for the audio interrupt: one writer (the main loop), one reader (app_render) */
enum { Q_ON, Q_OFF, Q_PATCH, Q_ALLOFF };
#define NEV 64u
static volatile uint32_t ev_q[NEV];
static volatile uint32_t ev_w, ev_r;
static void post(uint32_t kind, uint32_t a, uint32_t b)
{
    uint32_t w = ev_w;
    if (w - ev_r >= NEV)
        return;                              /* full: dropped (64 in one 6 ms block: not from a person) */
    ev_q[w % NEV] = kind | a << 8 | b << 16;
    ev_w = w + 1u;
}

static volatile int32_t last_note = -1;      /* for the screen */
static volatile uint32_t midi_last, midi_last_port, midi_count;

static void midi_in(void)                    /* audio interrupt */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (st != 0xF8u && st != 0xFEu) {
            midi_last = pkt;
            midi_last_port = (uint32_t)port;
            midi_count++;
        }
        switch (st & 0xF0u) {
        case 0x90u:
            if (d2) {
                sq80_note_on(&syn, (int)d1, (int)d2);
                last_note = (int32_t)d1;
            } else {
                sq80_note_off(&syn, (int)d1);   /* velocity 0 is a note off */
            }
            break;
        case 0x80u:
            sq80_note_off(&syn, (int)d1);
            break;
        case 0xB0u:
            sq80_control_change(&syn, (int)d1, (int)d2);
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
            sq80_set_patch(&syn, &patches[a % NPATCH]);
            break;
        case Q_ALLOFF:
            sq80_all_notes_off(&syn);
            break;
        }
        ev_r = ev_r + 1u;
    }
}

static volatile uint32_t voices_now;

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    events();
    midi_in();
    if (n > APP_BLOCK)
        n = APP_BLOCK;
    sq80_render(&syn, buf_l, buf_r, (int)n);
    for (i = 0; i < n; i++) {
        float l, r;
        gain_now += (gain - gain_now) * 0.004f;
        l = buf_l[i] * 0.8f * gain_now;      /* eight loud voices clip: the clamp below */
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

/* --------------------------------------------------------------- settings --- */
static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.octave = 0;
    set.patch = 0;
}

void app_init(void)
{
    sq80_rompack_load(&rom, pack_buf, sq80_test_pack(pack_buf));
    sq80_init(&syn, &rom, 44100.0f);
    make_patches();
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.octave < -2 || set.octave > 2 || set.patch >= NPATCH)
        settings_defaults();
    sq80_set_patch(&syn, &patches[set.patch]);
}

/* --------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint8_t stress;                      /* ARP: the eight-note chord held */
    uint32_t peak;                       /* the CPU load's peak since HOME */
    uint32_t cpu_shown, voices_shown, xruns_shown, peak_shown;
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

static void octave_text(char *t)
{
    t[0] = set.octave < 0 ? '-' : '+';
    t[1] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
    t[2] = 0;
}

static void speak(void)
{
    char t[96], n[12];
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME ". ");
        text_cat(t, PATCH_NAME[set.patch]);
        text_cat(t, ". CPU ");
        int_text((int32_t)plat_cpu_pct(), n);
        text_cat(t, n);
        text_cat(t, " PERCENT. PEAK ");
        int_text((int32_t)ui.peak, n);
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
    octave_text(t);
    app_say("OCTAVE", t);
}

static void patch_step(int32_t d)
{
    set.patch = (uint8_t)(((int32_t)set.patch + d + NPATCH * 8) % NPATCH);
    set_dirty = 1;
    ui.stress = 0;
    post(Q_PATCH, set.patch, 0);
    app_say("PATCH", PATCH_NAME[set.patch]);
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
            ui.peak = 0;
            app_say("PEAK", "RESET");
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
        patch_step(e > 0 ? 1 : -1);
    for (k = 0; k < NE; k++)                 /* the other encoders: nothing yet (read, so they do not pile up) */
        if (k != EN_PRESET)
            (void)plat_enc((int)k);
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);
    plat_leds(btn | (set_dirty ? 1u << B_SAVE : 0u) | (ui.stress ? 1u << B_ARP : 0u), keys);
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

static void bar(int32_t y, uint32_t pct, uint32_t peak)
{
    int32_t w = (int32_t)(pct > 100u ? 100u : pct) * 220 / 100, pk = (int32_t)(peak > 100u ? 100u : peak) * 220 / 100;
    cv_rect(10, y, 220, 14, C_LINE);
    cv_rect(10, y, w, 14, pct >= 85u ? RGB(230, 70, 60) : C_HI);
    if (pk > 1)
        cv_rect(10 + pk - 2, y, 2, 14, C_WHITE);
}

static void draw(void)
{
    char t[32], n[12];
    uint32_t now = plat_ms(), cpu = plat_cpu_pct();
    /* the header: the name, or a message for a moment */
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        cv_text(10, 6, &FONT_B, APP_NAME, C_WHITE);
        if (last_note >= 0) {
            note_name(last_note, t);
            cv_text(230 - text_w(&FONT_B, t), 6, &FONT_B, t, C_HI);
        }
    }
    cv_blit(0, 0);
    /* the patch, and the load */
    cv_begin(240, 150, C_BLACK);
    cv_text(10, 4, &FONT_XS, "PATCH", C_GRAY);
    t[0] = 0;
    int_text(set.patch + 1, n);
    text_cat(t, n);
    text_cat(t, " ");
    text_cat(t, PATCH_NAME[set.patch]);
    cv_text(10, 18, &FONT_B, t, C_WHITE);
    cv_text(10, 52, &FONT_XS, "CPU", C_GRAY);
    int_text((int32_t)cpu, t);
    text_cat(t, "%");
    cv_text(44, 46, &FONT_B, t, cpu >= 85u ? RGB(230, 70, 60) : C_HI);
    cv_text(120, 52, &FONT_XS, "PEAK", C_GRAY);
    int_text((int32_t)ui.peak, t);
    text_cat(t, "%");
    cv_text(158, 46, &FONT_B, t, C_WHITE);
    bar(78, cpu, ui.peak);
    cv_text(10, 102, &FONT_XS, "VOICES", C_GRAY);
    int_text((int32_t)voices_now, t);
    text_cat(t, " / 8");
    cv_text(66, 102, &FONT_XS, t, C_WHITE);
    cv_text(120, 102, &FONT_XS, "XRUN", C_GRAY);
    int_text((int32_t)plat_xruns(), t);
    cv_text(158, 102, &FONT_XS, t, plat_xruns() ? RGB(230, 70, 60) : C_WHITE);
    cv_text(10, 124, &FONT_XS, "STRESS (ARP)", C_GRAY);
    cv_text(120, 124, &FONT_XS, ui.stress ? "ON: 8 VOICES" : "OFF", ui.stress ? C_HI : C_DIM);
    cv_blit(0, 36);
    /* the footer: octave, MIDI in, the help */
    cv_begin(240, 52, C_BLACK);
    octave_text(n);
    cv_text(10, 2, &FONT_XS, "OCT", C_GRAY);
    cv_text(38, 2, &FONT_XS, n, C_HI);
    if (midi_count) {
        static const char *const PORT[NPORTS] = {"USB", "BLE", "TRS"};
        cv_text(80, 2, &FONT_XS, "MIDI IN", C_GRAY);
        cv_text(130, 2, &FONT_XS, midi_last_port < NPORTS ? PORT[midi_last_port] : "", C_HI);
    }
    cv_text(10, 20, &FONT_XS, "PRESET: PATCH   HOME: PEAK RESET", C_DIM);
    cv_text(10, 34, &FONT_XS, "ARP: STRESS     SAVE: KEEP", C_DIM);
    cv_blit(0, 188);
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
    uint32_t now = plat_ms(), cpu = plat_cpu_pct(), x = plat_xruns(), v = voices_now;
    input();
    speak();
    if (cpu > ui.peak)
        ui.peak = cpu;
    if (midi_count != last_midi) {
        last_midi = midi_count;
        ui.redraw = 1;
    }
    if (now - last_tick >= 250u &&            /* the numbers, four times a second at most */
        (cpu != ui.cpu_shown || v != ui.voices_shown || x != ui.xruns_shown || ui.peak != ui.peak_shown)) {
        last_tick = now;
        ui.cpu_shown = cpu;
        ui.voices_shown = v;
        ui.xruns_shown = x;
        ui.peak_shown = ui.peak;
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
    else if (!strcmp(what, "patch"))
        *got = set.patch;
    else if (!strcmp(what, "voices"))
        *got = (int)voices_now;
    else if (!strcmp(what, "note"))
        *got = last_note;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "stress"))
        *got = ui.stress;
    else
        return 0;
    return 1;
}
#endif
