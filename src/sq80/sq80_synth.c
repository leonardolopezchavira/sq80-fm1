/* SPDX-License-Identifier: MIT
 * The SQ-80 voice engine. Per voice: three DOC 5503-style wavetable oscillators at the DOC's own
 * rate (38455.855 Hz), resampled to the output rate; a 4-pole resonant low-pass; the final DCA
 * with pan; four envelopes and three LFOs at the SQ-80's control rate (83.59 Hz).
 * Every mapping constant is in sq80_cal.h (measured, docs/measurements.md). */

#include "sq80.h"
#include "sq80_port.h"
#include "sq80_cal.h"

#define PI_F 3.14159265358979f

enum { ST_IDLE, ST_ATTACK, ST_DECAY1, ST_DECAY2, ST_SUSTAIN, ST_RELEASE, ST_RELEASE2, ST_DONE };

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int level_bytes(int c) { return c >= 0 ? 2 * c : 2 * (c - 1); }   /* -63..63 -> -128..126 */
static float semitone_ratio(float semis) { return sq80_exp2f(semis * (1.0f / 12.0f)); }

static float dca_gain(float level) {
    int i = 1;
    float t, db;
    if (level <= 0.0f) return 0.0f;
    if (level >= 63.0f) return 1.0f;
    while (level > SQ80_DCA_LEVEL[i]) ++i;
    t = (level - SQ80_DCA_LEVEL[i - 1]) / (SQ80_DCA_LEVEL[i] - SQ80_DCA_LEVEL[i - 1]);
    db = SQ80_DCA_DB[i - 1] + (SQ80_DCA_DB[i] - SQ80_DCA_DB[i - 1]) * t;
    return sq80_exp2f(db * (3.32192809f / 20.0f));
}

/* FILTER FREQ steps (fractional, may be out of 0..127) to the cutoff parameter in Hz */
static float cutoff_hz(float steps) {
    int i;
    if (steps <= 0.0f) return SQ80_CUTOFF_HZ[0] * sq80_exp2f(steps * (1.0f / 12.0f));
    if (steps >= 127.0f) return SQ80_CUTOFF_HZ[127];
    i = (int)steps;
    return SQ80_CUTOFF_HZ[i] + (SQ80_CUTOFF_HZ[i + 1] - SQ80_CUTOFF_HZ[i]) * (steps - (float)i);
}

/* ------------------------------------------------------------------------- envelope */

static void env_segment(sq80_env *e, int target, int time) {
    int d;
    time = clampi(time, 0, 63);
    e->target = target;
    e->ticks = SQ80_ENV_TICKS[time] > 0 ? SQ80_ENV_TICKS[time] : 1;
    d = target * 256 - e->value;
    e->step = (d < 0 ? -d : d) / e->ticks;
}

static void env_begin_release(sq80_env *e) {
    if (e->p.second_release) {   /* "R": half the level minus 12 in T4, then to 0 in time 39 */
        e->stage = ST_RELEASE;
        env_segment(e, e->value / 512 - 12, e->p.t4);
    } else {
        e->stage = ST_RELEASE2;
        env_segment(e, 0, e->p.t4);
    }
}

static void env_start(sq80_env *e, const sq80_env_params *p, int key, int velocity, int cycle, int keep_level) {
    e->p = *p;
    e->levels[0] = 0;
    e->levels[1] = level_bytes(p->l1);
    e->levels[2] = level_bytes(p->l2);
    e->levels[3] = level_bytes(p->l3);
    velocity = clampi(velocity, 0, 127);
    /* velocity level scaling: LV 0 = none (252/256), LV 63 = fully velocity dependent */
    e->scale = ((p->lv * velocity) >> 5) + (63 - p->lv) * 4;
    e->tk_offset = p->tk * (key > 36 ? 2 * (key - 36) : 0) / 256;
    e->t1_offset = (p->t1v * velocity) >> 7;
    e->cycle = (uint8_t)(cycle != 0);
    e->released = 0;
    e->active = 1;
    if (!keep_level) e->value = 0;
    e->stage = ST_ATTACK;
    env_segment(e, e->levels[1] * e->scale / 256, p->t1 - e->t1_offset);
}

static void env_next(sq80_env *e) {
    switch (e->stage) {
    case ST_ATTACK:
        e->stage = ST_DECAY1;
        env_segment(e, e->levels[2] * e->scale / 256, e->p.t2 - e->tk_offset);
        break;
    case ST_DECAY1:
        e->stage = ST_DECAY2;
        env_segment(e, e->levels[3] * e->scale / 256, e->p.t3 - e->tk_offset);
        break;
    case ST_DECAY2:
        if (e->value == 0) e->stage = ST_DONE;
        else if (e->cycle) env_begin_release(e);   /* CYC (full cycle): no sustain */
        else e->stage = ST_SUSTAIN;
        break;
    case ST_RELEASE:
        e->stage = ST_RELEASE2;
        env_segment(e, 0, SQ80_SECOND_RELEASE_TIME);
        break;
    case ST_RELEASE2:
        e->stage = ST_DONE;
        break;
    default:
        break;
    }
}

static void env_release(sq80_env *e) {
    if (!e->active || e->cycle || e->released) return;   /* a full-cycle envelope ignores the key */
    e->released = 1;
    if (e->stage == ST_RELEASE || e->stage == ST_RELEASE2 || e->stage == ST_DONE) return;
    env_begin_release(e);
}

static int env_tick(sq80_env *e) {
    if (!e->active) return e->output = 0;
    if (e->stage == ST_DONE) {
        e->active = 0;
        e->value = 0;
        return e->output = 0;
    }
    if (e->stage != ST_SUSTAIN) {
        const int goal = e->target * 256;
        if (e->value < goal) e->value = e->value + e->step > goal ? goal : e->value + e->step;
        else if (e->value > goal) e->value = e->value - e->step < goal ? goal : e->value - e->step;
        if (--e->ticks <= 0) {
            e->value = goal;
            env_next(e);
        }
    }
    e->output = e->value / 256;
    return e->output;
}

/* ------------------------------------------------------------------------- LFO */

static void lfo_start(sq80_lfo *l, const sq80_lfo_params *p, int first_note) {
    l->p = *p;
    if (p->reset || first_note) l->phase = 0;
    l->level = p->l1 * 2;
    l->level_target = p->l2 * 2;
    l->emu_clock = 0;
}

static int lfo_tick(sq80_lfo *l, int mod_source) {
    /* rate: linear in FREQ, 0.33 Hz at 7 and 18.6 Hz at 63 (83.59 Hz control rate) */
    const int f = (l->p.freq << 8) + l->human_offset;
    const uint32_t inc = f < 0x700 ? (uint32_t)(((uint32_t)f << 16) / 7u) : (uint32_t)(f - 0x600) << 16;
    const uint32_t before = l->phase;
    int b, v, amp;
    l->phase += inc;   /* one cycle = 2^32 */
    b = (int)(l->phase >> 24);
    switch (l->p.wave) {
    case 0: v = ((b < 0x80 ? b : (~b & 0xff)) - 0x40) * 2; break;   /* TRI */
    case 1: v = (int8_t)(uint8_t)(b + 0x80); break;                 /* SAW */
    case 2: v = b < 0x80 ? 127 : 0; break;                          /* SQR (unipolar) */
    default:                                                        /* NOISE */
        if (((l->phase ^ before) & 0x80000000u) || inc >= 0x80000000u) {
            l->noise = l->noise * 1664525u + 1013904223u;
            l->noise_held = (int8_t)(l->noise >> 24);
        }
        v = l->noise_held;
        break;
    }
    /* delayed fade from L1 to L2: one step of DELAY every 7.125 ticks (the SQ-80's LFO update) */
    l->emu_clock -= 1024;
    if (l->emu_clock <= 0) {
        l->emu_clock += 7296;
        if (l->level < l->level_target) l->level = l->level + l->p.delay > l->level_target ? l->level_target : l->level + l->p.delay;
        else if (l->level > l->level_target) l->level = l->level - l->p.delay < l->level_target ? l->level_target : l->level - l->p.delay;
        if (l->p.humanize && l->p.freq >= 7) {
            l->noise = l->noise * 1664525u + 1013904223u;
            l->human_offset = (int)((l->noise >> 30) % 3u) * 128;   /* 0..+1 FREQ unit */
        }
    }
    amp = l->level + (mod_source > 0 ? mod_source : 0);
    l->output = amp <= 0 ? 0 : clampi(v * amp / 256, -127, 127);
    return l->output;
}

/* ------------------------------------------------------------------------- filter */

static void filter_set(sq80_filter *f, float hz, float resonance, float sample_rate) {
    float g, G;
    hz = clampf(hz, 10.0f, sample_rate * 0.45f);
    g = sq80_tanf(PI_F * hz / sample_rate);
    G = g / (1.0f + g);
    f->k = resonance;
    f->G = G;
    f->G2 = G * G;
    f->G3 = f->G2 * G;
    f->G4 = f->G2 * f->G2;
    f->inv = 1.0f / (1.0f + g);
    f->den = 1.0f / (1.0f + resonance * f->G4);
}

/* soft clip, division-free: x - 4x^3/27 up to |x| = 1.5, then +-1 (tanh's slope at 0, flat at the knee) */
static float soft_clip(float x) {
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (4.0f / 27.0f) * x * x * x;
}

static float filter_process(sq80_filter *f, float x) {
    /* 4-pole zero-delay-feedback ladder with passband compensation and a soft input; the coefficients
     * (filter_set, every 16 samples) leave no division per sample */
    const float G = f->G, k = f->k;
    const float S = (f->G3 * f->s[0] + f->G2 * f->s[1] + G * f->s[2] + f->s[3]) * f->inv;
    const float xin = (1.0f + SQ80_FILTER_BASS_COMPENSATION * k) * x;
    const float y4 = (f->G4 * xin + S) * f->den;
    float u = soft_clip(SQ80_FILTER_DRIVE * (xin - k * y4)) * (1.0f / SQ80_FILTER_DRIVE);
    int i;
    for (i = 0; i < 4; ++i) {
        const float v = (u - f->s[i]) * G;
        const float y = v + f->s[i];
        f->s[i] = y + v;
        u = y;
    }
    return u;
}

/* ------------------------------------------------------------------------- synth */

void sq80_init(sq80_synth *s, const sq80_rompack *rom, float sample_rate) {
    sq80_zero(s, sizeof *s);
    s->rom = rom;
    s->sample_rate = sample_rate;
    s->doc_step = SQ80_DOC_RATE / sample_rate;
    s->samples_per_tick = sample_rate / SQ80_CONTROL_RATE;
    s->amp_pole = sq80_powf(0.01f, 1.0f / (0.002f * SQ80_DOC_RATE + 1.0f));   /* -40 dB in 2 ms */
    s->volume = 127;
    s->last_key = -1;
    sq80_default_patch(&s->patch);
}

void sq80_set_patch(sq80_synth *s, const sq80_patch *p) { s->patch = *p; }

int sq80_active_voices(const sq80_synth *s) {
    int n = 0, i;
    for (i = 0; i < SQ80_VOICES; ++i) n += s->voices[i].active ? 1 : 0;
    return n;
}

static int mod_source(const sq80_synth *s, const sq80_voice *v, uint8_t src) {
    switch (src) {
    case SQ80_LFO1: case SQ80_LFO2: case SQ80_LFO3: return v->lfo[src - SQ80_LFO1].output;
    case SQ80_ENV1: case SQ80_ENV2: case SQ80_ENV3: case SQ80_ENV4: return v->env[src - SQ80_ENV1].output;
    case SQ80_VEL: return v->velocity;
    case SQ80_VELX: return v->velocity * v->velocity / 127;
    case SQ80_KBD: return v->key;
    case SQ80_KBD2: return v->key <= 32 ? -128 : v->key >= 95 ? 124 : (v->key - 64) * 4;
    case SQ80_WHEEL: return s->wheel;
    case SQ80_PEDAL: return s->pedal;
    case SQ80_XCTRL: return s->xctrl;
    case SQ80_PRESS: return s->pressure;
    default: return 0;
    }
}

static float mod_sum(const sq80_synth *s, const sq80_voice *v, const sq80_mod *a, const sq80_mod *b) {
    return (float)(mod_source(s, v, a->source) * a->depth + mod_source(s, v, b->source) * b->depth) * (1.0f / 64.0f);
}

static void setup_osc(sq80_synth *s, sq80_voice *v, int i, float pitch) {
    const sq80_osc_params *op = &s->patch.osc[i];
    sq80_osc *o = &v->osc[i];
    const int zone_key = clampi(v->key + op->octave * 12 + op->semi, 0, 127);
    const int raw = s->rom->zones[op->wave * SQ80_ZONES + (zone_key >> 3)];
    const sq80_raw_wave w = sq80_rompack_raw(s->rom, raw);
    float semis, hz, inc;
    if (raw != o->raw_index) {
        o->raw_index = raw;
        o->start = w.start;
        o->shift = (uint8_t)(32 - w.size_log2);
    }
    o->one_shot = op->wave >= 54;
    semis = pitch + (float)w.tune_semi + (float)w.tune_frac * (1.0f / 256.0f) - (float)SQ80_A4_KEY;
    /* DOC: a table pass takes 2^(17 + resolution) accumulator steps: each resolution step halves
     * the rate. Single-cycle waves (resolution 1) are the reference. */
    hz = SQ80_A4 * semitone_ratio(semis) * sq80_exp2f(1.0f - (float)w.resolution);
    inc = hz / SQ80_DOC_RATE * 4294967296.0f;
    o->inc = inc >= 2147483647.0f ? 2147483647u : (uint32_t)inc;
}

static void control_tick(sq80_synth *s, sq80_voice *v) {
    const sq80_patch *p = &s->patch;
    float env4, gain, pan_steps, pan, cutoff_steps;
    int i;
    for (i = 0; i < 4; ++i) env_tick(&v->env[i]);
    for (i = 0; i < 3; ++i) lfo_tick(&v->lfo[i], mod_source(s, v, p->lfo[i].mod));
    if (!v->env[3].active) {
        v->active = 0;
        return;
    }
    if (v->glide_rate > 0.0f) {
        const float target = (float)v->key;
        if (v->pitch_key < target) v->pitch_key = v->pitch_key + v->glide_rate > target ? target : v->pitch_key + v->glide_rate;
        else v->pitch_key = v->pitch_key - v->glide_rate < target ? target : v->pitch_key - v->glide_rate;
        if (v->pitch_key == target) v->glide_rate = 0.0f;
    }
    for (i = 0; i < 3; ++i) {
        const sq80_osc_params *op = &p->osc[i];
        const float pitch = v->pitch_key + s->bend_semis + (float)(op->octave * 12 + op->semi) + (float)op->fine / 32.0f +
                            mod_sum(s, v, &op->pitch_mod[0], &op->pitch_mod[1]) * SQ80_PITCH_MOD_SEMITONES_PER_UNIT;
        const float level = (float)op->level + mod_sum(s, v, &op->level_mod[0], &op->level_mod[1]) * SQ80_LEVEL_MOD_PER_UNIT;
        setup_osc(s, v, i, pitch);
        v->osc[i].amp_target = (op->output || (i == 0 && p->am)) ? dca_gain(level) : 0.0f;
    }
    if (p->am) v->osc[0].amp_target = v->osc[1].amp_target = dca_gain(63.0f);

    cutoff_steps = (float)p->cutoff + mod_sum(s, v, &p->filter_mod[0], &p->filter_mod[1]) * SQ80_CUTOFF_MOD_PER_UNIT +
                   (float)p->key_track / 63.0f * SQ80_KEY_TRACK_AT_MAX * (float)(v->key - SQ80_KEY_TRACK_CENTER);
    v->cutoff_target = cutoff_hz(cutoff_steps);
    v->resonance = SQ80_RESONANCE[p->resonance & 31];

    env4 = (float)(v->env[3].output > 0 ? v->env[3].output : 0) / 126.0f;
    gain = sq80_powf(env4 * ((float)p->final_level / 63.0f), SQ80_FINAL_EXPONENT) * ((float)s->volume / 127.0f) *
           SQ80_OUTPUT_GAIN;
    /* linear pan law: PAN 8 is the centre, 0 and 15 the sides */
    pan_steps = (float)p->pan + (float)(mod_source(s, v, p->pan_mod.source) * p->pan_mod.depth) / 64.0f * SQ80_PAN_MOD_PER_UNIT;
    pan = pan_steps >= 15.0f ? 1.0f : clampf(0.5f + (pan_steps - 8.0f) / 16.0f, 0.0f, 1.0f);
    v->gain_l_target = gain * 2.0f * (1.0f - pan);
    v->gain_r_target = gain * 2.0f * pan;
}

static void start_voice(sq80_synth *s, sq80_voice *v, int key, int velocity, int legato) {
    const sq80_patch *p = &s->patch;
    const int reuse = v->active;
    const float glide_ticks = p->glide > 0 ? (float)SQ80_ENV_TICKS[p->glide > 63 ? 63 : p->glide] : 0.0f;
    int i;
    v->key = key;
    v->velocity = velocity;
    v->age = ++s->age_counter;
    if (glide_ticks > 0.0f && s->last_key >= 0) {   /* glide from the last key played */
        if (!reuse) v->pitch_key = (float)s->last_key;
        v->glide_rate = sq80_fabsf((float)key - v->pitch_key) / glide_ticks;
    } else {
        v->pitch_key = (float)key;
        v->glide_rate = 0.0f;
    }
    if (legato) return;

    v->active = 1;
    v->released = 0;
    for (i = 0; i < 4; ++i) env_start(&v->env[i], &p->env[i], key, velocity, p->cycle, reuse && !p->restart_env);
    for (i = 0; i < 3; ++i) {
        lfo_start(&v->lfo[i], &p->lfo[i], !s->lfo_started[i]);
        s->lfo_started[i] = 1;
    }
    for (i = 0; i < 3; ++i) {
        sq80_osc *o = &v->osc[i];
        o->raw_index = -1;
        setup_osc(s, v, i, v->pitch_key + (float)(p->osc[i].octave * 12 + p->osc[i].semi));
        if (!reuse || p->restart_osc || o->one_shot) o->phase = 0;
        o->halted = 0;
        if (!reuse) o->amp = 0.0f;
    }
    if (!reuse) {
        sq80_zero(&v->filter, sizeof v->filter);
        sq80_zero(v->hist, sizeof v->hist);
        v->doc_phase = 0.0f;
        v->gain_l = v->gain_r = 0.0f;
    }
    control_tick(s, v);
    if (!reuse) v->cutoff = v->cutoff_target;
    filter_set(&v->filter, v->cutoff, v->resonance, s->sample_rate);
}

void sq80_note_on(sq80_synth *s, int key, int velocity) {
    sq80_voice *target = 0;
    int i;
    if (velocity <= 0) { sq80_note_off(s, key); return; }
    key = clampi(key, 0, 127);
    velocity = clampi(velocity, 1, 127);
    if (s->held_count < 16) s->held_keys[s->held_count++] = key;

    if (s->patch.mono) {
        sq80_voice *v = &s->voices[0];
        for (i = 1; i < SQ80_VOICES; ++i) s->voices[i].active = 0;
        start_voice(s, v, key, velocity, v->active && !v->released && s->held_count > 1);
        s->last_key = key;
        return;
    }
    if (s->patch.restart_voice)
        for (i = 0; i < SQ80_VOICES && !target; ++i)
            if (s->voices[i].active && s->voices[i].key == key) target = &s->voices[i];
    for (i = 0; i < SQ80_VOICES && !target; ++i)
        if (!s->voices[i].active) target = &s->voices[i];
    if (!target) {   /* steal: the oldest released voice, else the oldest */
        for (i = 0; i < SQ80_VOICES; ++i)
            if (s->voices[i].released && (!target || s->voices[i].age < target->age)) target = &s->voices[i];
        if (!target)
            for (i = 0; i < SQ80_VOICES; ++i)
                if (!target || s->voices[i].age < target->age) target = &s->voices[i];
        target->active = 0;   /* a stolen voice starts fresh */
    }
    start_voice(s, target, key, velocity, 0);
    s->last_key = key;
}

static void release_voice(sq80_voice *v) {
    int e;
    v->released = 1;
    for (e = 0; e < 4; ++e) env_release(&v->env[e]);
}

void sq80_note_off(sq80_synth *s, int key) {
    int i, j;
    for (i = 0; i < s->held_count; ++i)
        if (s->held_keys[i] == key) {
            for (j = i; j + 1 < s->held_count; ++j) s->held_keys[j] = s->held_keys[j + 1];
            --s->held_count;
            break;
        }
    if (s->patch.mono) {
        sq80_voice *v = &s->voices[0];
        if (v->active && v->key == key && s->held_count > 0) {   /* back to the newest held key */
            start_voice(s, v, s->held_keys[s->held_count - 1], v->velocity, 1);
            return;
        }
    }
    for (i = 0; i < SQ80_VOICES; ++i) {
        sq80_voice *v = &s->voices[i];
        if (!v->active || v->key != key || v->released) continue;
        if (s->sustain >= 64) v->released = 1;   /* the envelopes release when the pedal goes up */
        else release_voice(v);
    }
}

void sq80_control_change(sq80_synth *s, int cc, int value) {
    int i, was_down;
    value = clampi(value, 0, 127);
    switch (cc) {
    case 1: s->wheel = value; break;
    case 2: s->xctrl = value; break;
    case 4: s->pedal = value; break;
    case 7: s->volume = value; break;
    case 64:
        was_down = s->sustain >= 64;
        s->sustain = value;
        if (was_down && value < 64)
            for (i = 0; i < SQ80_VOICES; ++i)
                if (s->voices[i].active && s->voices[i].released) release_voice(&s->voices[i]);
        break;
    case 120: case 123: sq80_all_notes_off(s); break;
    default: break;
    }
}

void sq80_pitch_bend(sq80_synth *s, int value14) {
    s->bend_semis = (float)(clampi(value14, 0, 16383) - 8192) / 8192.0f * 2.0f;   /* +-2 semitones */
}

void sq80_channel_pressure(sq80_synth *s, int value) { s->pressure = clampi(value, 0, 127); }

void sq80_all_notes_off(sq80_synth *s) {
    int i;
    s->held_count = 0;
    for (i = 0; i < SQ80_VOICES; ++i)
        if (s->voices[i].active) release_voice(&s->voices[i]);
}

static float render_doc(sq80_synth *s, sq80_voice *v) {
    const sq80_patch *p = &s->patch;
    float sum = 0.0f, s0 = 0.0f;
    int i;
    for (i = 0; i < 3; ++i) {
        sq80_osc *o = &v->osc[i];
        uint32_t before;
        uint8_t smp;
        float x, a;
        o->amp = o->amp_target + (o->amp - o->amp_target) * s->amp_pole;
        o->wrapped = 0;
        if (o->halted) continue;
        before = o->phase;
        o->phase += o->inc;
        o->wrapped = o->phase < before;
        if (o->wrapped && o->one_shot && !(i == 1 && p->sync)) { o->halted = 1; continue; }
        if (i == 1 && p->sync && v->osc[0].wrapped) o->phase = 0;
        smp = sq80_rom_at(s->rom, o->start + (o->phase >> o->shift));
        if (smp == 0) { o->halted = 1; continue; }   /* a zero byte stops the DOC oscillator */
        x = (float)((int)smp - 128) * (1.0f / 127.0f);
        if (i == 0) s0 = x;
        if (i == 0 && p->am) continue;   /* OSC1 only modulates */
        a = o->amp;
        if (i == 1 && p->am) a *= s0 * 0.6f + 0.4f;
        if (p->osc[i].output || (i == 1 && p->am)) sum += x * a;
    }
    return sum;
}

/* one voice over m samples (no control tick among them), added into left and right */
static void render_voice(sq80_synth *s, sq80_voice *v, float *left, float *right, int m, uint32_t fc) {
    const float doc_step = s->doc_step;
    float h0 = v->hist[0], h1 = v->hist[1], h2 = v->hist[2], h3 = v->hist[3], ph = v->doc_phase;
    float cutoff = v->cutoff, gl = v->gain_l, gr = v->gain_r;
    const float ct = v->cutoff_target, glt = v->gain_l_target, grt = v->gain_r_target;
    int k;
    for (k = 0; k < m; ++k) {
        float c1, c2, c3, x, y;
        ph += doc_step;
        while (ph >= 1.0f) {
            ph -= 1.0f;
            h0 = h1;
            h1 = h2;
            h2 = h3;
            h3 = render_doc(s, v);
        }
        /* cubic Hermite between h1 and h2 */
        c1 = 0.5f * (h2 - h0);
        c2 = h0 - 2.5f * h1 + 2.0f * h2 - 0.5f * h3;
        c3 = 0.5f * (h3 - h0) + 1.5f * (h1 - h2);
        x = ((c3 * ph + c2) * ph + c1) * ph + h1;

        cutoff += (ct - cutoff) * 0.002f;
        if (((fc + (uint32_t)k) & 15u) == 0) filter_set(&v->filter, cutoff, v->resonance, s->sample_rate);
        y = filter_process(&v->filter, x);
        gl += (glt - gl) * 0.004f;
        gr += (grt - gr) * 0.004f;
        left[k] += y * gl;
        right[k] += y * gr;
    }
    v->hist[0] = h0; v->hist[1] = h1; v->hist[2] = h2; v->hist[3] = h3;
    v->doc_phase = ph;
    v->cutoff = cutoff;
    v->gain_l = gl;
    v->gain_r = gr;
}

/* Voices first .. first+count-1 over n samples into left and right (cleared first). Reads the synth's
 * shared state and writes only those voices: two calls on disjoint voices may run at once (two cores).
 * The block's control ticks fall where sq80_render puts them; sq80_render_commit then moves the clock. */
void sq80_render_part(sq80_synth *s, float *left, float *right, int n, int first, int count) {
    float countdown = s->control_countdown;
    uint32_t fc = s->filter_counter;
    int k = 0, i, last = first + count;
    if (first < 0) first = 0;
    if (last > SQ80_VOICES) last = SQ80_VOICES;
    for (i = 0; i < n; ++i) left[i] = right[i] = 0.0f;
    while (k < n) {
        int m = 1;
        /* this sample: a control tick? then the samples after it up to the next tick, voice by voice */
        countdown -= 1.0f;
        if (countdown <= 0.0f) {
            countdown += s->samples_per_tick;
            for (i = first; i < last; ++i)
                if (s->voices[i].active) control_tick(s, &s->voices[i]);
        }
        while (k + m < n && countdown - 1.0f > 0.0f) {
            countdown -= 1.0f;
            ++m;
        }
        for (i = first; i < last; ++i)
            if (s->voices[i].active) render_voice(s, &s->voices[i], left + k, right + k, m, fc + 1u);
        fc += (uint32_t)m;
        k += m;
    }
}

void sq80_render_commit(sq80_synth *s, int n) {
    int k;
    for (k = 0; k < n; ++k) {
        s->control_countdown -= 1.0f;
        if (s->control_countdown <= 0.0f) s->control_countdown += s->samples_per_tick;
    }
    s->filter_counter += (uint32_t)n;
}

void sq80_render(sq80_synth *s, float *left, float *right, int n) {
    sq80_render_part(s, left, right, n, 0, SQ80_VOICES);
    sq80_render_commit(s, n);
}

/* ------------------------------------------------------------------------- the filter, drawn */

float sq80_filter_cutoff(int freq, int keybd, int key) {
    float hz = cutoff_hz((float)clampi(freq, 0, 127) +
                         (float)clampi(keybd, 0, 63) / 63.0f * SQ80_KEY_TRACK_AT_MAX * (float)(key - SQ80_KEY_TRACK_CENTER));
    return hz > SQ80_DOC_RATE * 0.45f ? SQ80_DOC_RATE * 0.45f : hz;   /* (filter_set's limit) */
}

float sq80_filter_db(float hz, float cutoff, int res) {
    /* H(jw) = (1 + c k) / ((1 + jw/wc)^4 + k): (1 + jx)^4 = (1 - 6x^2 + x^4) + j 4x (1 - x^2) */
    const float k = SQ80_RESONANCE[res & 31], x = hz / (cutoff > 1.0f ? cutoff : 1.0f), x2 = x * x;
    const float re = 1.0f - 6.0f * x2 + x2 * x2 + k, im = 4.0f * x * (1.0f - x2), g = 1.0f + SQ80_FILTER_BASS_COMPENSATION * k;
    float m = g * g / (re * re + im * im + 1e-12f);
    return 3.0103f * sq80_log2f(m > 1e-12f ? m : 1e-12f);
}

void sq80_filter_curve(int freq, int keybd, int res, int key, signed char *db, int n) {
    const float fc = sq80_filter_cutoff(freq, keybd, key);
    float hz = 20.0f, r;
    int i;
    if (n < 2) return;
    r = sq80_exp2f(9.965784f / (float)(n - 1));   /* (1000 ^ (1 / (n - 1)): 20 Hz to 20 kHz) */
    for (i = 0; i < n; ++i, hz *= r) {
        float d = sq80_filter_db(hz, fc, res);
        db[i] = (signed char)(d > 60.0f ? 60 : d < -120.0f ? -120 : (int)(d + (d < 0.0f ? -0.5f : 0.5f)));
    }
}
