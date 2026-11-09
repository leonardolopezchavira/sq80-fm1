/* SPDX-License-Identifier: GPL-3.0-only
 * SQ-80 for the FM-1: SLOOP's arpeggiator, step sequencer and live recording, for one synth part.
 *
 * Ported from SLOOP (https://github.com/isod89/sloop-fm1, firmware/src/seq.c and core.h),
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, GPL-3.0-only; its MIDI clock after Felucca
 * 1.0's midi_clock.c (contributions by ChanceTheMaker and keremimo). Changes for the SQ-80 Copyright (C) 2026
 * Leonardo López Chavira, GPL-3.0-only.
 *
 * What it keeps of SLOOP: the transport clock (a unit is a sample at 1 BPM, so a beat is BEAT_U units at any tempo
 * and every division of it a whole number of units: no drift), its grids and MPC swing (grid_swing, grid_at,
 * trk_grid), the arpeggiator with its 14 modes and its probability, HOLD and ORDER (arp_add, arp_remove, arp_list,
 * arp_pick, arp_step, arp_tick), the steps of up to 4 notes with ties, slides, accents and ratchets (seq_step,
 * seq_ratchets, seq_tick), live recording into the nearest step as heard, with held notes as ties and a short
 * release put back (rec_target, step_add, rec_note, rec_hold, rec_release), the one-level undo, and following a
 * MIDI clock (mclk_*). What it leaves out: SLOOP's four tracks (one part here: the SQ-80), the drum track, nudges,
 * parameter locks, fills, rolls, the free take and the count-in, the scale keyboard, and 2.7's merging of a recording
 * over notes held in the pattern (here a recorded note into a tied step starts a step of its own).
 *
 * The SQ-80's render places the events to the sample: sl_next says how far the next one is, the app renders up to
 * it and calls sl_run (SLOOP ticks once per block; here a tick falls where the event is due). */
#include "sloop_seq.h"

/* (no libc headers in the FM-1's -O2 units: our own) */
static void mem_zero(void *d, uint32_t n)
{
    uint8_t *p = (uint8_t *)d;
    while (n--)
        *p++ = 0;
}
static void mem_copy(void *d, const void *s, uint32_t n)
{
    uint8_t *p = (uint8_t *)d;
    const uint8_t *q = (const uint8_t *)s;
    while (n--)
        *p++ = *q++;
}

#define FS 44100u
#define BEAT_U (FS * 60u)
#define SEQ_NONE 0xFFFFFFFFu
#define REC_LAT 512u             /* key to ear, samples (SLOOP's): a recording takes it back */

static sl_song *sg;
#define P(i) ((uint32_t)sg->p[i])

static uint32_t clamp_u(int32_t v, int32_t lo, int32_t hi) { return (uint32_t)(v < lo ? lo : v > hi ? hi : v); }

static uint32_t rng_state = 0x1234567u;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* ------------------------------------------------------------ settings --- */
static const char *const ONOFF[] = {"OFF", "ON"};
static const char *const AMODES[] = {"UP", "DOWN", "UP/DN", "RANDOM", "ORDER", "REPEAT", "DN/UP",
                                     "UP+8", "CONV", "DIVG", "PINKY", "THUMB", "WALK", "CHORD"};
static const char *const RATES[] = {"1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/2", "1 BAR", "2 BARS"};
static const char *const ORDERS[] = {"SORTED", "PLAYED"};
static const char *const CLICKS[] = {"OFF", "REC", "ON"};
static const sl_param_info INFO[SL_NP] = {
    {"ARP", 0, 1, 0, ONOFF},          {"MODE", 1, 14, 1, AMODES},     {"RATE", 0, 5, 2, RATES},
    {"OCTAVES", 1, 4, 1, 0},          {"GATE", 1, 127, 64, 0},        {"SWING", 0, 100, 0, 0},
    {"HOLD", 0, 1, 0, ONOFF},         {"ORDER", 0, 1, 0, ORDERS},     {"CHANCE", 0, 127, 127, 0},
    {"LENGTH", 1, 64, 16, 0},         {"DIV", 0, 8, 2, RATES},        {"GATE", 1, 127, 64, 0},
    {"SWING", 0, 100, 0, 0},          {"TEMPO", 40, 240, 120, 0},     {"MIDI CLK", 0, 1, 1, ONOFF},
    {"CLICK", 0, 2, 0, CLICKS}};

const sl_param_info *sl_param(uint32_t id) { return &INFO[id % SL_NP]; }

void sl_defaults(sl_song *s)
{
    uint32_t i;
    for (i = 0; i < SL_NP; i++)
        s->p[i] = INFO[i].def;
    mem_zero(s->step, sizeof s->step);
    for (i = 0; i < SL_NSTEP; i++)
        s->step[i].time = SL_ST_REST;
}

void sl_clamp(sl_song *s)
{
    uint32_t i;
    for (i = 0; i < SL_NP; i++)
        if (s->p[i] < INFO[i].min || s->p[i] > INFO[i].max)
            s->p[i] = INFO[i].def;
    for (i = 0; i < SL_NSTEP; i++) {
        sl_step *st = &s->step[i];
        if (st->time > SL_ST_REST || st->n > 4u) {
            mem_zero(st, sizeof *st);
            st->time = SL_ST_REST;
        }
    }
}

static void cat(char *d, const char *s)
{
    while (*d)
        d++;
    while ((*d++ = *s++) != 0)
        ;
}
static void num(int32_t v, char *t)
{
    char b[12];
    uint32_t i = 0, u = (uint32_t)(v < 0 ? -v : v);
    do {
        b[i++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    if (v < 0)
        *t++ = '-';
    while (i)
        *t++ = b[--i];
    *t = 0;
}

const char *sl_value_text(uint32_t id, int32_t v, char *t)
{
    const sl_param_info *in = sl_param(id);
    if (in->names)
        return in->names[(uint32_t)(v - in->min) % (uint32_t)(in->max - in->min + 1)];
    if (id == SL_AGATE || id == SL_SGATE || id == SL_APROB) {   /* (out of 128, or 127 for the chance: as %) */
        num((v * 100 + 64) / (id == SL_APROB ? 127 : 128), t);
        cat(t, "%");
    } else {
        num(v, t);
        if (id == SL_ASWING || id == SL_SSWING)
            cat(t, "%");
    }
    return t;
}

/* --------------------------------------------------------------- clock --- */
static volatile uint32_t clk_beat, clk_pos;          /* beats since PLAY, units into it */
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* a step: 1 / DEN beats */
#define NDIV_SHORT 6u
#define NDIV_STEP 9u
static const uint8_t DIV_BEATS[3] = {2, 4, 8};
static uint32_t div_units(uint32_t div)
{
    return div < NDIV_SHORT ? BEAT_U / DIV_DEN[div] : BEAT_U * DIV_BEATS[(div - NDIV_SHORT) % 3u];
}

static volatile uint8_t playing, recording, armed;
static uint32_t click_beat = 0xFFFFFFFFu;            /* the beat the metronome last clicked */
static int clicking(void) { return P(SL_CLICK) == 2u || (P(SL_CLICK) == 1u && recording); }
static uint32_t blk_n, blk_u, blk_done;                /* this block: samples, units in all, samples gone */

/* units an odd step starts late: MPC swing (0 = 50 %, 100 = 75 %) */
static uint32_t swing_units(int32_t pct, uint32_t u) { return clamp_u(pct, 0, 100) * u / 200u; }
/* a position on a grid of steps u units long, odd steps sw late: the step, units into it, its length */
static uint32_t grid_swing(uint32_t abs, uint32_t frac, uint32_t u, uint32_t sw, uint32_t *into, uint32_t *len)
{
    if (abs & 1u) {
        if (frac < sw) {
            abs--;
            frac += u;
            *len = u + sw;
        } else {
            frac -= sw;
            *len = u - sw;
        }
    } else {
        *len = u + sw;
    }
    *into = frac;
    return abs;
}
static uint32_t grid_den(uint32_t den, uint32_t sw, uint32_t *into, uint32_t *len)
{
    uint32_t u = BEAT_U / den;
    return grid_swing(clk_beat * den + clk_pos / u, clk_pos % u, u, sw, into, len);
}
static uint32_t grid_at(uint32_t div, uint32_t sw, uint32_t *into, uint32_t *len)
{
    uint32_t m;
    if (div < NDIV_SHORT)
        return grid_den(DIV_DEN[div], sw, into, len);
    m = DIV_BEATS[(div - NDIV_SHORT) % 3u];
    return grid_swing(clk_beat / m, (clk_beat % m) * BEAT_U + clk_pos, BEAT_U * m, sw, into, len);
}
/* swing is for the straight grids inside a beat (off on the triplets and on whole beats) */
static uint32_t swings(uint32_t div) { return div < 4u; }
static uint32_t trk_div(void) { return P(SL_SDIV) % NDIV_STEP; }
static uint32_t trk_grid(uint32_t *into, uint32_t *len)
{
    uint32_t div = trk_div();
    return grid_at(div, swings(div) ? swing_units((int32_t)P(SL_SSWING), div_units(div)) : 0u, into, len);
}
static uint32_t trk_len(void) { return P(SL_SLEN) > 0u ? (P(SL_SLEN) > SL_NSTEP ? SL_NSTEP : P(SL_SLEN)) : 1u; }

/* ------------------------------------------------------------ MIDI clock --- */
#define MCLK_PULSE_U (BEAT_U / 24u)
#define MCLK_NB 8u
static struct {
    uint32_t pos, done;
    uint32_t last_ms, iv_ms, beat_ms;
    uint8_t have, n24, alive, nb, bi;
    uint16_t bdt[MCLK_NB];
} mclk;
static uint32_t now_ms;
static volatile uint8_t transport_req;               /* 1 start, 2 stop (from the clock, for the next block) */

static int mclk_on(void) { return P(SL_SYNC) && mclk.alive && now_ms - mclk.last_ms < 500u; }

static void mclk_tempo(uint32_t dt)
{
    uint32_t k, sum = 0, t10;
    int32_t one = (int32_t)((60000u + dt / 2u) / dt), cur = (int32_t)P(SL_BPM);
    if (mclk.nb && (one - cur >= 5 || cur - one >= 5))
        mclk.nb = 0;
    mclk.bdt[mclk.bi] = (uint16_t)dt;
    mclk.bi = (uint8_t)((mclk.bi + 1u) % MCLK_NB);
    if (mclk.nb < MCLK_NB)
        mclk.nb++;
    for (k = 0; k < mclk.nb; k++)
        sum += mclk.bdt[(mclk.bi + MCLK_NB - 1u - k) % MCLK_NB];
    t10 = (600000u * mclk.nb + sum / 2u) / sum;
    if (mclk.nb < MCLK_NB || (int32_t)t10 - cur * 10 >= 8 || cur * 10 - (int32_t)t10 >= 8)
        sg->p[SL_BPM] = (uint8_t)clamp_u((int32_t)((t10 + 5u) / 10u), 40, 240);
}

void sl_realtime(uint32_t st, uint32_t ms)
{
    now_ms = ms;
    if (!sg || !P(SL_SYNC))
        return;
    if (st == 0xFAu || st == 0xFBu) {                 /* START: from the top; CONTINUE: on */
        mclk.pos = mclk.done = 0;
        mclk.have = 0;
        transport_req = 1;
        return;
    }
    if (st == 0xFCu) {
        transport_req = 2;
        return;
    }
    if (st != 0xF8u)
        return;
    if (mclk.alive && ms - mclk.last_ms < 200u) {
        uint32_t iv = ms - mclk.last_ms;
        mclk.iv_ms = mclk.iv_ms ? (mclk.iv_ms * 3u + iv + 2u) / 4u : iv;
    }
    if (!mclk.alive || ms - mclk.last_ms >= 500u) {
        mclk.n24 = 0;
        mclk.nb = 0;
        mclk.beat_ms = ms;
    } else if (++mclk.n24 == 24u) {
        uint32_t dt = ms - mclk.beat_ms;
        mclk.n24 = 0;
        mclk.beat_ms = ms;
        if (dt >= 250u && dt <= 1500u)
            mclk_tempo(dt);
    }
    mclk.alive = 1;
    mclk.last_ms = ms;
    if (playing || transport_req == 1u) {
        if (mclk.have)
            mclk.pos += MCLK_PULSE_U;
        mclk.have = 1;                                /* the first pulse after START is the downbeat */
    }
}

static uint32_t mclk_adv(uint32_t n)                  /* units to advance this block (mclk_on) */
{
    uint32_t el, off = 0, tgt, adv, cap = n * 2u * P(SL_BPM);
    if (!mclk.have)
        return 0;
    el = now_ms - mclk.last_ms;
    if (mclk.iv_ms) {
        if (el > mclk.iv_ms)
            el = mclk.iv_ms;
        off = el * MCLK_PULSE_U / mclk.iv_ms;
        if (off >= MCLK_PULSE_U)
            off = MCLK_PULSE_U - 1u;
    }
    tgt = mclk.pos + off;
    adv = (int32_t)(tgt - mclk.done) > 0 ? tgt - mclk.done : 0u;
    if (adv > cap)
        adv = cap;
    mclk.done += adv;
    return adv;
}

/* --------------------------------------------------------------- state --- */
static struct {
    /* the arpeggiator */
    uint8_t held[16], nheld, arp_phys;
    uint8_t arp_note, arp_ch[3], arp_new, arp_walk, arp_odd, arp_den;
    uint32_t arp_idx, arp_abs, arp_pos, arp_off;
    /* the sequencer */
    uint32_t seq_abs, seq_off, pass;
    uint16_t seq_idx;
    uint8_t seq_den, seq_n, seq_hold, seq_notes[4], rat_done[4];
    uint8_t rskip[4], rskip_n;
    uint32_t rskip_abs;
    /* recording: the notes held */
    uint8_t rh_note[4], rh_n, rh_start, rh_last, rh_ties;
    uint32_t rh_last_abs;
    sl_step rh_bak;
    /* the keys: down now, the last chord (step entry) */
    uint8_t down, last[4], nlast, last_vel;
} T;
static volatile uint32_t changes;
static volatile uint8_t entry_on, cursor;            /* step entry: on (the UI's STEP page), the step it writes */
static uint8_t entry_open;                           /* .. a chord being written (the keys not all let go) */

static struct {
    uint8_t valid, undone;
    uint8_t len;
    uint32_t sess;
    sl_step st[SL_NSTEP];
} undo;
static uint32_t undo_sess = 1;
static uint32_t undo_sess_next(void) { return undo_sess += 4u; }
static void undo_mark(uint32_t sess)
{
    if (undo.valid && !undo.undone && undo.sess == sess)
        return;
    mem_copy(undo.st, sg->step, sizeof undo.st);
    undo.len = sg->p[SL_SLEN];
    undo.sess = sess;
    undo.valid = 1;
    undo.undone = 0;
}
static void undo_swap(void)
{
    static sl_step tmp[SL_NSTEP];
    uint8_t l = sg->p[SL_SLEN];
    mem_copy(tmp, sg->step, sizeof tmp);
    mem_copy(sg->step, undo.st, sizeof tmp);
    mem_copy(undo.st, tmp, sizeof tmp);
    sg->p[SL_SLEN] = undo.len;
    undo.len = l;
    undo.undone ^= 1u;
    changes++;
}
#define UNDO_REC (T.pass << 2 | 1u)

/* -------------------------------------------------------------- the arp --- */
enum { AM_OFF, AM_UP, AM_DN, AM_UPDN, AM_RND, AM_ORD, AM_REPEAT, AM_DNUP, AM_UP8, AM_CONV, AM_DIVG, AM_PINKY,
       AM_THUMB, AM_WALK, AM_CHORD };

static void rec_note(uint32_t note, uint32_t vel, uint32_t rat, int hold);

static void arp_add(uint32_t note)
{
    uint32_t i;
    if (P(SL_AHOLD) && T.arp_phys == 0u)
        T.nheld = 0;                                   /* a new chord replaces the latched one */
    T.arp_phys++;
    for (i = 0; i < T.nheld; i++)
        if (T.held[i] == note)
            return;
    if (T.nheld < 16u)
        T.held[T.nheld++] = (uint8_t)note;
    if (T.nheld == 1u) {
        T.arp_new = 1;                                 /* the first note: now (or on the grid just ahead) */
        T.arp_idx = 0xFFFFFFFFu;
    }
}

static void arp_remove(uint32_t note)
{
    uint32_t i, k = 0;
    if (T.arp_phys)
        T.arp_phys--;
    if (P(SL_AHOLD))
        return;
    for (i = 0; i < T.nheld; i++)
        if (T.held[i] != note)
            T.held[k++] = T.held[i];
    T.nheld = (uint8_t)k;
}

/* the note list: the held notes each once, sorted or as played, over OCT octaves (its length; *cnt the notes) */
static uint32_t arp_list(uint32_t *list, uint32_t *cnt)
{
    uint32_t n = 0, len = 0, i, j, o, last = 0, m = P(SL_AMODE);
    for (i = 0; i < T.nheld; i++) {
        uint32_t x = T.held[i];
        last = x;
        for (j = 0; j < n && list[j] != x; j++)
            ;
        if (j == n)
            list[n++] = x;
    }
    *cnt = n;
    if (!n)
        return 0;
    if (m == AM_REPEAT) {                              /* REPEAT: the last note played, no octaves */
        list[0] = last;
        return 1;
    }
    if (!P(SL_AORDER) && m != AM_ORD)
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && list[j - 1] > list[j]; j--) {
                uint32_t x = list[j];
                list[j] = list[j - 1];
                list[j - 1] = x;
            }
    for (o = 0; o < P(SL_AOCT); o++)
        for (i = 0; i < n && len < 64u; i++)
            list[len++] = clamp_u((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
    return len;
}

/* the place in the list the step arp_idx plays, by MODE (UP+8 adds its note to the list) */
static uint32_t arp_pick(uint32_t *list, uint32_t len)
{
    uint32_t i = T.arp_idx, m = P(SL_AMODE), cyc, k, w;
    if (m == AM_UP8)
        list[len++] = clamp_u((int32_t)list[0] + 12 * (int32_t)P(SL_AOCT), 0, 127);
    cyc = len > 1u ? 2u * len - 2u : 1u;
    k = i % cyc;
    switch (m) {
    case AM_DN:
        return len - 1u - i % len;
    case AM_UPDN:
        return k < len ? k : cyc - k;
    case AM_DNUP:
        return k < len ? len - 1u - k : k - (len - 1u);
    case AM_RND:
        return rng() % len;
    case AM_CONV:
    case AM_DIVG:
        k = i % len;
        if (m == AM_DIVG)
            k = len - 1u - k;
        return (k & 1u) ? len - 1u - k / 2u : k / 2u;
    case AM_PINKY:
        return len < 2u ? 0u : (k & 1u) ? len - 1u : k / 2u;
    case AM_THUMB:
        return len < 2u ? 0u : (k & 1u) ? 1u + k / 2u : 0u;
    case AM_WALK:
        w = T.arp_walk;
        if (!i)
            w = 0;
        else if (w >= len)
            w = len - 1u;
        else if (len > 1u)
            w = !w ? 1u : w == len - 1u ? w - 1u : (rng() >> 7) & 1u ? w + 1u : w - 1u;
        T.arp_walk = (uint8_t)w;
        return w;
    default:                                           /* UP, ORD, REPEAT */
        return i % len;
    }
}

static void arp_end(void)
{
    uint32_t i;
    for (i = 0; i < 3u; i++)
        if (T.arp_ch[i]) {
            sl_out_off(T.arp_ch[i]);
            T.arp_ch[i] = 0;
        }
    if (T.arp_note) {
        sl_out_off(T.arp_note);
        T.arp_note = 0;
    }
}

static void arp_play(uint32_t n)                       /* (recorded while recording: what you hear) */
{
    sl_out_on(n, 100);
    if (recording && playing)
        rec_note(n, 100, 0, 0);
}

static int arp_step(void)
{
    uint32_t list[65], cnt, len = arp_list(list, &cnt), i, o, top = 0;
    if (!len)
        return 0;
    T.arp_idx++;
    if (P(SL_AMODE) != AM_CHORD) {
        T.arp_note = (uint8_t)list[arp_pick(list, len)];
        if (!T.arp_note)
            T.arp_note = 1;                            /* (0 means none) */
        arp_play(T.arp_note);
        return 1;
    }
    o = T.arp_idx % (P(SL_AOCT) > 0u ? P(SL_AOCT) : 1u);
    for (i = 0; i < cnt && i < 4u; i++) {              /* CHORD: the held notes together, o octaves up */
        uint32_t nt = clamp_u((int32_t)list[i] + 12 * (int32_t)o, 1, 127);
        if (nt == 127u && top++)
            continue;
        if (!T.arp_note)
            T.arp_note = (uint8_t)nt;
        else
            T.arp_ch[i - 1u] = (uint8_t)nt;
        arp_play(nt);
    }
    return 1;
}

/* the arp's grid while playing: the step, units into it, its length */
static uint32_t arp_grid(uint32_t *into, uint32_t *slen)
{
    uint32_t div = P(SL_ARATE) % NDIV_SHORT, u = div_units(div);
    return grid_at(div, swings(div) ? swing_units((int32_t)P(SL_ASWING), u) : 0u, into, slen);
}

static void arp_tick(uint32_t adv)
{
    uint32_t div = P(SL_ARATE) % NDIV_SHORT, u = div_units(div), into, slen, abs = 0, fire = 0;
    if (T.arp_note) {
        if (T.arp_off <= adv)
            arp_end();
        else
            T.arp_off -= adv;
    }
    if (!P(SL_AHOLD) && !T.arp_phys)
        T.nheld = 0;                                   /* (HOLD turned off with no key down) */
    if (!P(SL_AON) || !T.nheld) {
        if (T.arp_note)
            arp_end();
        if (!P(SL_AON))
            T.nheld = T.arp_phys = 0;
        T.arp_new = 0;
        return;
    }
    if (playing) {
        abs = arp_grid(&into, &slen);
        if (T.arp_new) {
            T.arp_new = 0;
            T.arp_abs = into * 4u >= slen * 3u ? abs : abs - 1u;   /* the last quarter: the grid plays it */
        } else if (div != T.arp_den) {
            T.arp_abs = abs;
        }
        T.arp_den = (uint8_t)div;
        if (abs + 1u == T.arp_abs)
            abs = T.arp_abs;
        fire = abs != T.arp_abs;
        T.arp_abs = abs;
    } else {                                           /* stopped: from the first key, the odd steps sw late */
        uint32_t sw = swings(div) ? swing_units((int32_t)P(SL_ASWING), u) : 0u, lim;
        if (T.arp_new) {
            T.arp_new = 0;
            T.arp_odd = 0;
            T.arp_pos = u - sw;
        }
        lim = T.arp_odd ? u + sw : u - sw;
        T.arp_pos += adv;
        if (T.arp_pos >= lim) {
            T.arp_pos = T.arp_pos - lim < lim ? T.arp_pos - lim : 0;
            T.arp_odd ^= 1u;
            fire = 1;
        }
        slen = T.arp_odd ? u + sw : u - sw;
    }
    if (!fire)
        return;
    arp_end();
    if ((rng() & 127u) <= P(SL_APROB) && arp_step())
        T.arp_off = slen * P(SL_AGATE) / 128u;
}

/* ------------------------------------------------------------ recording --- */
/* the step a note played now goes into (as heard: REC_LAT earlier): the one playing, or the next when past the middle
 * of it; *later: that one has not played yet (it must not sound twice) */
static uint32_t rec_target(uint32_t *later)
{
    uint32_t into, slen, abs = trk_grid(&into, &slen), half = slen / 2u, lat = REC_LAT * P(SL_BPM);
    if (into > half + (lat < half ? lat : half))
        abs++;
    *later = abs != T.seq_abs;
    return abs;
}

/* a note into step idx (overdub: a chord of up to 4; full, the last one replaced) */
static void step_add(uint32_t idx, uint32_t note, uint32_t vel, uint32_t rat)
{
    sl_step *s = &sg->step[idx % SL_NSTEP];
    uint32_t k;
    if (s->time != SL_ST_NOTE || !s->n) {
        mem_zero(s, sizeof *s);
        s->time = SL_ST_NOTE;
    }
    for (k = 0; k < s->n && s->note[k] != note; k++)
        ;
    if (k == s->n) {
        if (s->n < 4u)
            s->n++;
        k = s->n - 1u;
        s->note[k] = (uint8_t)note;
    }
    s->lvl = (uint8_t)(s->lvl & ~(3u << (2u * k)));     /* (the step's velocity) */
    s->rat = (uint8_t)((s->rat & ~(3u << (2u * k))) | (rat & 3u) << (2u * k));
    if (vel > s->vel)
        s->vel = (uint8_t)vel;
    changes++;
}

/* live: the nearest step. Held, each further step the sequencer enters becomes a TIE (rec_hold), never over a step
 * with notes; let go before the middle of the last, that step is put back (rec_release). A note into another step
 * ends the hold before */
static void rec_note(uint32_t note, uint32_t vel, uint32_t rat, int hold)
{
    uint32_t len = trk_len(), later, abs = rec_target(&later), idx = abs % len, k;
    undo_mark(UNDO_REC);
    step_add(idx, note, vel, rat);
    if (later) {                                       /* it sounds now: the step must not trigger it again */
        if (T.rskip_abs != abs)
            T.rskip_n = 0;
        T.rskip_abs = abs;
        if (T.rskip_n < 4u)
            T.rskip[T.rskip_n++] = (uint8_t)note;
    }
    if (!hold)
        return;
    if (!T.rh_n || T.rh_start != idx) {
        T.rh_n = 0;
        T.rh_start = (uint8_t)idx;
        T.rh_ties = 0;
    }
    for (k = 0; k < T.rh_n && T.rh_note[k] != note; k++)
        ;
    if (k == T.rh_n && T.rh_n < 4u)
        T.rh_note[T.rh_n++] = (uint8_t)note;
}

/* the sequencer enters step idx: a recorded note still held ties into it */
static void rec_hold(uint32_t idx, uint32_t len, uint32_t abs)
{
    sl_step *s;
    if (!T.rh_n)
        return;
    if (!recording || T.rh_ties + 1u >= len) {
        T.rh_n = 0;
        return;
    }
    if (idx == T.rh_start)
        return;                                        /* (recorded ahead into the step starting now) */
    s = &sg->step[idx];
    if (s->time == SL_ST_NOTE && s->n) {
        T.rh_n = 0;                                    /* a step with notes: the hold ends before it */
        return;
    }
    T.rh_bak = *s;
    T.rh_last = (uint8_t)idx;
    T.rh_last_abs = abs;
    T.rh_ties++;
    mem_zero(s, sizeof *s);
    s->time = SL_ST_TIE;
    changes++;
}

static void rec_release(uint32_t note)
{
    uint32_t i, k = 0, into, slen, half, lat, early;
    for (i = 0; i < T.rh_n; i++)
        if (T.rh_note[i] != note)
            T.rh_note[k++] = T.rh_note[i];
    if (k == T.rh_n)
        return;
    T.rh_n = (uint8_t)k;
    if (k)
        return;                                        /* (a chord: held until its last key is up) */
    early = trk_grid(&into, &slen) == T.rh_last_abs;
    half = slen / 2u;
    lat = REC_LAT * P(SL_BPM);
    early = early && into <= half + (lat < half ? lat : half);
    if (T.rh_ties && early && sg->step[T.rh_last].time == SL_ST_TIE)
        sg->step[T.rh_last] = T.rh_bak;                /* let go early in it: not held into this step */
}

/* ------------------------------------------------------------ transport --- */
static void seq_release(void)
{
    uint32_t i;
    for (i = 0; i < T.seq_n; i++)
        sl_out_off(T.seq_notes[i]);
    T.seq_n = 0;
    T.seq_hold = 0;
}

static void seq_start(void)
{
    T.seq_abs = SEQ_NONE;
    T.seq_idx = 0;
    T.rskip_n = 0;
    T.rskip_abs = SEQ_NONE;
    T.rh_n = 0;
    T.arp_new = T.nheld != 0;
    clk_beat = 0;
    clk_pos = 0;
    click_beat = 0xFFFFFFFFu;
    mclk.done = mclk.pos;
    playing = 1;
}

static void seq_stop(void)
{
    playing = 0;
    recording = 0;
    seq_release();
    T.rh_n = 0;
    T.arp_new = T.nheld != 0;                          /* (the arp goes on from the keys) */
}

static uint32_t step_vel(const sl_step *s, uint32_t i)
{
    static const uint8_t V[4] = {0, 42, 72, 127};
    uint32_t base = (s->flags & SL_SF_ACCENT) ? 127u : (s->vel ? s->vel : 96u), l = (s->lvl >> (2u * i)) & 3u;
    return l ? V[l] : base;
}

/* one step: TIE extends, REST releases, NOTE (re)triggers; a SLIDE before makes it legato (the notes before held
 * until these start). skip: bit k = note k sounds already from live recording */
static void seq_step(const sl_step *s, uint32_t slen, uint32_t skip)
{
    uint32_t i, j, len = trk_len(), gate = slen * P(SL_SGATE) / 128u;
    uint32_t slide_in = T.seq_hold && T.seq_n;
    const sl_step *nx = &sg->step[(T.seq_idx + 1u) % len];
    uint32_t next_tie = nx->time == SL_ST_TIE;
    if (s->time == SL_ST_TIE) {
        if (T.seq_n) {
            T.seq_off = gate + slen / 2u;
            T.seq_hold = (s->flags & SL_SF_SLIDE) != 0 || next_tie;
        }
        return;
    }
    if (s->time == SL_ST_REST || !s->n) {
        seq_release();
        return;
    }
    if (s->rat) {                                      /* ratchets: each hit its share of the step */
        uint32_t hits = 1u + (s->rat & 3u);
        for (i = 1; i < s->n; i++)
            if (1u + ((s->rat >> (2u * i)) & 3u) > hits)
                hits = 1u + ((s->rat >> (2u * i)) & 3u);
        gate /= hits;
    }
    if (!slide_in)
        seq_release();
    for (i = 0; i < s->n; i++) {
        if ((skip >> i) & 1u)
            continue;
        if (slide_in) {                                /* (a slide into a note sounding: it goes on) */
            for (j = 0; j < T.seq_n && T.seq_notes[j] != s->note[i]; j++)
                ;
            if (j < T.seq_n)
                continue;
        }
        sl_out_on(s->note[i], step_vel(s, i));
    }
    if (slide_in)                                      /* what is not held over ends */
        for (i = 0; i < T.seq_n; i++) {
            for (j = 0; j < s->n && s->note[j] != T.seq_notes[i]; j++)
                ;
            if (j == s->n)
                sl_out_off(T.seq_notes[i]);
        }
    T.seq_n = 0;
    for (i = 0; i < s->n; i++)
        if (!((skip >> i) & 1u))
            T.seq_notes[T.seq_n++] = s->note[i];
    T.seq_off = gate ? gate : 1u;
    T.seq_hold = !s->rat && ((s->flags & SL_SF_SLIDE) != 0 || next_tie);
}

static void seq_ratchets(uint32_t into, uint32_t slen)
{
    const sl_step *s = &sg->step[T.seq_idx % SL_NSTEP];
    uint32_t i;
    if (s->time != SL_ST_NOTE || !s->rat)
        return;
    for (i = 0; i < s->n; i++) {
        uint32_t hits = 1u + ((s->rat >> (2u * i)) & 3u), h, j;
        if (hits == 1u)
            continue;
        h = into * hits / slen;
        if (h > T.rat_done[i] && h < hits) {
            T.rat_done[i] = (uint8_t)h;
            for (j = 0; j < T.seq_n && T.seq_notes[j] != s->note[i]; j++)
                ;
            if (j < T.seq_n)
                sl_out_off(s->note[i]);
            sl_out_on(s->note[i], step_vel(s, i));
            T.seq_off = slen / hits * P(SL_SGATE) / 128u;
            if (!T.seq_off)
                T.seq_off = 1;
            if (j == T.seq_n && T.seq_n < 4u)
                T.seq_notes[T.seq_n++] = s->note[i];
        }
    }
}

/* the steps fire in order, one a tick at most (the ticks fall where they are due: sl_next) */
static void seq_tick(uint32_t adv)
{
    uint32_t len = trk_len(), into, slen, abs, idx, nabs = 0, fire = 0, div;
    if (T.seq_n && !T.seq_hold) {
        if (T.seq_off <= adv)
            seq_release();
        else
            T.seq_off -= adv;
    }
    if (!playing)
        return;
    abs = trk_grid(&into, &slen);
    div = trk_div();
    if (T.seq_abs != SEQ_NONE && div != T.seq_den)
        T.seq_abs = abs;                               /* DIV changed: the next step of the new grid plays */
    T.seq_den = (uint8_t)div;
    if (T.seq_abs == SEQ_NONE) {
        nabs = abs;
        fire = 1;
    } else {
        nabs = T.seq_abs + 1u;
        fire = abs == nabs || (int32_t)(abs - nabs) > 0;
    }
    if (fire) {
        const sl_step *s;
        uint32_t skip = 0, i, k;
        T.seq_abs = nabs;
        idx = nabs % len;
        T.seq_idx = (uint16_t)idx;
        T.rat_done[0] = T.rat_done[1] = T.rat_done[2] = T.rat_done[3] = 0;
        if (!idx)
            T.pass++;                                  /* a new pass of the loop (recording: one undo) */
        rec_hold(idx, len, nabs);
        s = &sg->step[idx];
        if (T.rskip_n && T.rskip_abs == nabs)
            for (i = 0; i < s->n; i++)
                for (k = 0; k < T.rskip_n; k++)
                    if (s->note[i] == T.rskip[k])
                        skip |= 1u << i;
        T.rskip_n = 0;
        seq_step(s, slen, skip);
    }
    if (T.seq_abs == abs)
        seq_ratchets(into, slen);
}

/* ---------------------------------------------------------------- input --- */
static void undo_mark(uint32_t sess);
static uint32_t undo_sess_next(void);

/* step entry, acid style (SLOOP's seq_entry): the notes held together become the cursor's step */
static void entry_note(uint32_t note, uint32_t vel)
{
    sl_step *st = &sg->step[cursor % trk_len()];
    if (!entry_open) {
        entry_open = 1;
        undo_mark(undo_sess_next());
        st->n = 0;
        st->lvl = st->rat = 0;
        st->vel = 0;
        st->time = SL_ST_NOTE;
    }
    if (st->n < 4u)
        st->note[st->n++] = (uint8_t)note;
    if (vel > st->vel)
        st->vel = (uint8_t)vel;
    changes++;
}

void sl_key(uint32_t note, uint32_t vel)
{
    if (!sg || note > 127u)
        return;
    if (vel) {
        if (!T.down)
            T.nlast = 0;                               /* a new chord for the step keys */
        if (T.nlast < 4u)
            T.last[T.nlast++] = (uint8_t)note;
        T.last_vel = (uint8_t)vel;
        T.down++;
        if (entry_on && !recording && !armed)
            entry_note(note, vel);
        if (armed && !playing) {                       /* armed: the first note is the downbeat */
            armed = 0;
            seq_start();
            recording = 1;
            T.pass++;
        }
        if (P(SL_AON)) {
            arp_add(note);
            return;
        }
        if (recording && playing)
            rec_note(note, vel, 0, 1);
        sl_out_on(note, vel);
    } else {
        if (T.down)
            T.down--;
        if (entry_open && !T.down) {           /* all let go: the next step */
            entry_open = 0;
            cursor = (uint8_t)((cursor + 1u) % trk_len());
            changes++;
        }
        rec_release(note);
        if (P(SL_AON)) {
            arp_remove(note);
            return;
        }
        sl_out_off(note);
    }
}

void sl_all_off(void)
{
    arp_end();
    seq_release();
}

void sl_init(sl_song *s)
{
    sg = s;
    mem_zero(&T, sizeof T);
    T.seq_abs = SEQ_NONE;
    T.rskip_abs = SEQ_NONE;
    T.nlast = 1;
    T.last[0] = 60;
    T.last_vel = 100;
    playing = recording = armed = 0;
}

/* ------------------------------------------------------------ commands --- */
static void step_type(sl_step *s, int32_t d)          /* NOTE, ACCENT, SLIDE, TIE in turn */
{
    uint32_t t = s->time == SL_ST_TIE ? 3u : (s->flags & SL_SF_SLIDE) ? 2u : (s->flags & SL_SF_ACCENT) ? 1u : 0u;
    t = (t + (uint32_t)(d > 0 ? 1 : 3)) % 4u;
    if (t == 3u) {
        s->time = SL_ST_TIE;
        return;
    }
    if (s->time != SL_ST_NOTE || !s->n) {
        s->time = SL_ST_NOTE;
        if (!s->n) {
            s->n = 1;
            s->note[0] = T.last[0];
        }
    }
    s->flags = (uint8_t)(s->flags & ~(SL_SF_ACCENT | SL_SF_SLIDE));
    s->flags |= (uint8_t)(t == 1u ? SL_SF_ACCENT : t == 2u ? SL_SF_SLIDE : 0u);
}

void sl_cmd(uint32_t cmd, uint32_t a, int32_t d)
{
    sl_step *s;
    uint32_t i;
    if (!sg)
        return;
    s = &sg->step[a % SL_NSTEP];
    switch (cmd) {
    case SC_PLAY:
        if (playing) {
            seq_stop();
        } else {
            seq_start();
            if (armed) {
                armed = 0;
                recording = 1;
                T.pass++;
            }
        }
        return;
    case SC_STOP:
        if (playing)
            seq_stop();
        armed = 0;
        return;
    case SC_REC:
        if (playing) {
            recording ^= 1u;
            T.rh_n = 0;
            if (recording)
                T.pass++;                              /* (a new undo) */
        } else {
            armed ^= 1u;
        }
        return;
    case SC_MARK:
        undo_mark(undo_sess += 4u);
        return;
    case SC_UNDO:
        if (undo.valid && !undo.undone)
            undo_swap();
        return;
    case SC_REDO:
        if (undo.valid && undo.undone)
            undo_swap();
        return;
    case SC_WIPE:
        undo_mark(undo_sess += 4u);
        for (i = 0; i < SL_NSTEP; i++) {
            mem_zero(&sg->step[i], sizeof sg->step[i]);
            sg->step[i].time = SL_ST_REST;
        }
        seq_release();
        changes++;
        return;
    case SC_SET:
        mem_zero(s, sizeof *s);
        s->time = SL_ST_NOTE;
        for (i = 0; i < T.nlast && i < 4u; i++)
            s->note[i] = T.last[i];
        s->n = (uint8_t)(T.nlast ? T.nlast : 1u);
        if (!T.nlast)
            s->note[0] = 60;
        s->vel = T.last_vel;
        break;
    case SC_CLEAR:
        mem_zero(s, sizeof *s);
        s->time = SL_ST_REST;
        break;
    case SC_NOTE:
        if (s->time != SL_ST_NOTE || !s->n)
            return;
        for (i = 0; i < s->n; i++)
            s->note[i] = (uint8_t)clamp_u((int32_t)s->note[i] + d, 1, 127);
        break;
    case SC_VEL:
        if (s->time != SL_ST_NOTE || !s->n)
            return;
        s->vel = (uint8_t)clamp_u((int32_t)(s->vel ? s->vel : 96u) + d, 1, 127);
        s->lvl = 0;
        break;
    case SC_RAT: {
        uint32_t r;
        if (s->time != SL_ST_NOTE || !s->n)
            return;
        r = clamp_u((int32_t)(s->rat & 3u) + (d > 0 ? 1 : -1), 0, 3);
        s->rat = (uint8_t)(r | r << 2 | r << 4 | r << 6);
        break;
    }
    case SC_TYPE:
        step_type(s, d);
        break;
    case SC_CURSOR: {
        int32_t len = (int32_t)trk_len(), c = ((int32_t)cursor + d) % len;
        cursor = (uint8_t)(c < 0 ? c + len : c);
        entry_open = 0;
        changes++;
        return;
    }
    case SC_TIME: {                                    /* NOTE, TIE, REST */
        uint32_t t = (s->time + (uint32_t)(d > 0 ? 1 : 2)) % 3u;
        if (t == SL_ST_NOTE && !s->n) {
            s->n = 1;
            s->note[0] = T.nlast ? T.last[0] : 60u;
            s->vel = T.last_vel;
        }
        s->time = (uint8_t)t;
        break;
    }
    case SC_FLAG: {                                    /* -, ACCENT, SLIDE, both */
        uint32_t f = (uint32_t)(s->flags & (SL_SF_ACCENT | SL_SF_SLIDE));
        if (s->time != SL_ST_NOTE || !s->n)
            return;
        f = (f + (uint32_t)(d > 0 ? 1 : 3)) % 4u;
        s->flags = (uint8_t)((s->flags & ~(SL_SF_ACCENT | SL_SF_SLIDE)) | f);
        break;
    }
    default:
        return;
    }
    changes++;
}

/* -------------------------------------------------------------- timing --- */
static void clk_move(uint32_t adv)
{
    uint32_t p = clk_pos + adv;
    while (p >= BEAT_U) {
        p -= BEAT_U;
        clk_beat++;
    }
    clk_pos = p;
}

void sl_block(uint32_t n, uint32_t ms)
{
    now_ms = ms;
    if (!sg)
        return;
    if (transport_req) {
        uint32_t r = transport_req;
        transport_req = 0;
        if (r == 1u && !playing)
            sl_cmd(SC_PLAY, 0, 0);
        else if (r == 2u && playing)
            seq_stop();
    }
    blk_n = n ? n : 1u;
    blk_done = 0;
    blk_u = playing && mclk_on() ? mclk_adv(blk_n) : blk_n * P(SL_BPM);
}

/* units ahead -> samples ahead at this block's rate, rounded up (32 bits: the FM-1's core has no 64-bit divide; a block
 * moves at most 2 x 256 x 240 units, so (u % blk_u) x blk_n fits) */
static uint32_t to_samples(uint32_t u)
{
    uint32_t q;
    if (!blk_u)
        return 1u << 30;
    q = u / blk_u;
    if (q >= (1u << 30) / blk_n)
        return 1u << 30;
    return q * blk_n + ((u % blk_u) * blk_n + blk_u - 1u) / blk_u;
}

uint32_t sl_next(void)
{
    uint32_t u = 0xFFFFFFFFu, into, slen;
    if (!sg)
        return 1u << 30;
    if (T.arp_note && T.arp_off < u)
        u = T.arp_off;
    if (T.seq_n && !T.seq_hold && T.seq_off < u)
        u = T.seq_off;
    if (P(SL_AON) && T.nheld) {
        if (T.arp_new)
            return 0;
        if (playing) {
            arp_grid(&into, &slen);
            if (slen - into < u)
                u = slen - into;
        } else {
            uint32_t div = P(SL_ARATE) % NDIV_SHORT, du = div_units(div);
            uint32_t sw = swings(div) ? swing_units((int32_t)P(SL_ASWING), du) : 0u, lim = T.arp_odd ? du + sw : du - sw;
            uint32_t left = T.arp_pos < lim ? lim - T.arp_pos : 0u;
            if (left < u)
                u = left;
        }
    }
    if (playing) {
        uint32_t abs;
        if (T.seq_abs == SEQ_NONE)
            return 0;
        if (clicking() && BEAT_U - clk_pos < u)
            u = BEAT_U - clk_pos;
        abs = trk_grid(&into, &slen);
        if (slen - into < u)
            u = slen - into;
        if (T.seq_abs == abs) {                        /* the next ratchet */
            const sl_step *s = &sg->step[T.seq_idx % SL_NSTEP];
            uint32_t i;
            if (s->time == SL_ST_NOTE && s->rat)
                for (i = 0; i < s->n; i++) {
                    uint32_t hits = 1u + ((s->rat >> (2u * i)) & 3u), h = into * hits / slen, b;
                    if (hits > 1u && h + 1u < hits) {
                        b = ((h + 1u) * slen + hits - 1u) / hits;   /* (<= 4 x 2 bars: fits) */
                        if (b > into && b - into < u)
                            u = b - into;
                    }
                }
        }
    }
    return u == 0xFFFFFFFFu ? 1u << 30 : to_samples(u);
}

void sl_run(uint32_t m)
{
    uint32_t adv;
    if (!sg)
        return;
    if (blk_done + m > blk_n)
        m = blk_n - blk_done;
    adv = blk_u * (blk_done + m) / blk_n - blk_u * blk_done / blk_n;   /* (blk_u x 256 fits 32 bits) */
    blk_done += m;
    if (playing)
        clk_move(adv);
    if (playing && clk_beat != click_beat) {           /* a new beat: the metronome */
        click_beat = clk_beat;
        if (clicking())
            sl_out_click((clk_beat & 3u) == 0u);
    }
    arp_tick(adv);
    seq_tick(adv);
}

/* ---------------------------------------------------------------- state --- */
uint32_t sl_state(void)
{
    uint32_t s = 0;
    if (playing)
        s |= SL_S_PLAY;
    if (recording)
        s |= SL_S_REC;
    if (armed)
        s |= SL_S_ARMED;
    if (playing && clk_pos < BEAT_U / 6u)
        s |= SL_S_BEAT;
    if (sg && mclk_on())
        s |= SL_S_SYNC;
    if (undo.valid && !undo.undone)
        s |= SL_S_UNDO;
    if (undo.valid && undo.undone)
        s |= SL_S_REDO;
    return s;
}

void sl_entry(int on)
{
    entry_on = (uint8_t)(on != 0);
}
uint32_t sl_cursor(void) { return sg ? cursor % trk_len() : 0u; }

int32_t sl_pos(void) { return playing && T.seq_abs != SEQ_NONE ? (int32_t)T.seq_idx : -1; }
uint32_t sl_changes(void) { return changes; }
