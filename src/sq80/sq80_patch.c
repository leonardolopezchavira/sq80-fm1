/* SPDX-License-Identifier: MIT
 * ROM pack reader, program decoder and SysEx reader. */

#include "sq80.h"
#include "sq80_port.h"

static uint32_t rd32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int sq80_rompack_load(sq80_rompack *pack, const uint8_t *data, size_t size) {
    uint32_t version, header, waves, raws, rom_size, i;
    size_t body;
    sq80_zero(pack, sizeof *pack);
    if (size < 32 || !sq80_same(data, "SQ80PACK", 8)) return 0;
    version = rd32(data + 8);
    header = rd32(data + 12);
    waves = rd32(data + 16);
    raws = rd32(data + 20);
    rom_size = rd32(data + 24);
    if (version != 1 || header != 32 || waves != SQ80_WAVES || raws == 0 || raws > 255 ||
        rom_size < 4096u || rom_size > SQ80_WAVE_ROM_SIZE || (rom_size & (rom_size - 1u)) != 0)
        return 0;
    body = 6u * SQ80_WAVES + 16u * SQ80_WAVES + 4u * raws;
    body += (16 - body % 16) % 16;
    if (size < header + body + rom_size) return 0;
    pack->names = data + header;
    pack->zones = pack->names + 6 * SQ80_WAVES;
    pack->raws = pack->zones + 16 * SQ80_WAVES;
    pack->rom = data + header + body;
    pack->rom_mask = rom_size - 1u;
    pack->raw_count = (int)raws;
    for (i = 0; i < SQ80_WAVES * SQ80_ZONES; ++i)
        if (pack->zones[i] >= raws) {
            sq80_zero(pack, sizeof *pack);
            return 0;
        }
    return 1;
}

sq80_raw_wave sq80_rompack_raw(const sq80_rompack *pack, int index) {
    const uint8_t *r = pack->raws + 4 * index;
    sq80_raw_wave w;
    w.start = ((uint32_t)(r[1] >> 6) << 16) | ((uint32_t)r[0] << 8);
    w.size_log2 = (uint8_t)(8 + ((r[1] >> 3) & 7));
    w.resolution = r[1] & 7;
    w.tune_semi = (int8_t)r[2];
    w.tune_frac = r[3];
    return w;
}

void sq80_rompack_name(const sq80_rompack *pack, int wave, char out[7]) {
    sq80_copy(out, pack->names + 6 * wave, 6);
    out[6] = 0;
}

/* ------------------------------------------------------------------------- test pack */

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

size_t sq80_test_pack(uint8_t *buf) {
    /* raw waves: [page, wave select (bank 7-6, size 5-3, resolution 2-0), tune semitones, tune 1/256] */
    static const uint8_t raws[4][4] = {
        {0x00, (2 << 3) | 1, 0, 0},   /* 0: 1024-byte sine, pages 0..3 */
        {0x04, (1 << 3) | 1, 0, 0},   /* 1: 512-byte saw, pages 4..5 */
        {0x06, (1 << 3) | 1, 0, 0},   /* 2: 512-byte square, pages 6..7 */
        {0x10, (4 << 3) | 4, 0, 0}};  /* 3: 4096-byte decaying one-shot, pages 16..31 */
    uint8_t *names = buf + 32, *zones = names + 6 * SQ80_WAVES, *raw = zones + 16 * SQ80_WAVES;
    uint8_t *rom = buf + 32 + 1680;   /* where sq80_rompack_load finds it: the directory padded to 16 */
    int w, z, i;
    sq80_zero(buf, SQ80_TEST_PACK_SIZE);
    sq80_copy(buf, "SQ80PACK", 8);
    wr32(buf + 8, 1); wr32(buf + 12, 32); wr32(buf + 16, SQ80_WAVES); wr32(buf + 20, 4); wr32(buf + 24, 8192);
    for (w = 0; w < SQ80_WAVES; ++w) {
        uint8_t r = (uint8_t)(w == 0 ? 1 : (w == 3 || w == 4) ? 2 : w >= 54 ? 3 : 0);
        names[6 * w] = 'T';
        names[6 * w + 1] = 'E';
        names[6 * w + 2] = 'S';
        names[6 * w + 3] = 'T';
        names[6 * w + 4] = (char)('0' + w / 10);
        names[6 * w + 5] = (char)('0' + w % 10);
        for (z = 0; z < SQ80_ZONES; ++z) zones[16 * w + z] = r;
    }
    sq80_copy(raw, raws, sizeof raws);
    /* sine and one-shot from a quarter-wave table by symmetry, without libm: a Bhaskara approximation */
    for (i = 0; i < 1024; ++i) {
        int q = i & 511, x = q <= 256 ? q : 512 - q;   /* 0..256 over a half period */
        /* sin(pi*x/512) ~ 16 x (512 - x) / (5*512^2 - 4 x (512 - x)) */
        int32_t num = 16 * x * (512 - x), den = 5 * 512 * 512 - 4 * x * (512 - x);
        int v = (int)(126 * num / den);
        rom[i] = (uint8_t)(128 + (i < 512 ? v : -v));
    }
    for (i = 0; i < 512; ++i) rom[0x400 + i] = (uint8_t)(1 + i * 254 / 511);
    for (i = 0; i < 512; ++i) rom[0x600 + i] = (uint8_t)(i < 256 ? 228 : 28);
    for (i = 0; i < 4095; ++i) {
        int amp = 120 - i * 120 / 4095, v = rom[(i * 16) & 1023] - 128;
        int s = 128 + v * amp / 126;
        rom[0x1000 + i] = (uint8_t)(s < 1 ? 1 : s);
    }
    rom[0x1fff] = 0;   /* the one-shot's terminator */
    return SQ80_TEST_PACK_SIZE;
}

/* ------------------------------------------------------------------------- programs */

static int s8(uint8_t b) { return b >= 128 ? b - 256 : b; }
static int s7(uint8_t b) { b &= 0x7f; return b >= 64 ? b - 128 : b; }
static int half(uint8_t b) { return s8(b) / 2; }   /* C division rounds toward zero, as the SQ-80 */
static int8_t depth(int v) { return (int8_t)(v < -63 ? -63 : v > 63 ? 63 : v); }
static uint8_t src(int v) { return (uint8_t)(v & 15); }

void sq80_decode_patch(const uint8_t p[102], sq80_patch *out) {
    int i;
    sq80_zero(out, sizeof *out);
    for (i = 0; i < 6; ++i) out->name[i] = (p[i] >= 0x20 && p[i] < 0x7f) ? (char)p[i] : ' ';
    for (i = 0; i < 4; ++i) {
        const uint8_t *b = p + 6 + 10 * i;
        sq80_env_params *v = &out->env[i];
        v->l1 = depth(half(b[0]));
        v->l2 = depth(half(b[1]));
        v->l3 = depth(half(b[2]));
        v->t1 = b[3] & 63;
        v->t2 = b[4] & 63;
        v->t3 = b[5] & 63;
        v->t4 = b[6] & 63;
        v->second_release = (b[6] & 0x80) != 0;
        v->lv = b[7] >> 2;
        v->t1v = b[8] & 63;
        v->tk = b[9] & 63;
    }
    for (i = 0; i < 3; ++i) {
        const uint8_t *b = p + 0x2e + 4 * i;
        sq80_lfo_params *v = &out->lfo[i];
        v->freq = b[0] & 63;
        v->wave = b[0] >> 6;
        v->l1 = b[1] & 63;
        v->l2 = b[2] & 63;
        v->mod = src(((b[1] >> 6) << 2) | (b[2] >> 6));
        v->delay = b[3] & 63;
        v->humanize = (b[3] & 0x40) != 0;
        v->reset = (b[3] & 0x80) != 0;
    }
    for (i = 0; i < 3; ++i) {
        const uint8_t *b = p + 0x3a + 10 * i;
        sq80_osc_params *v = &out->osc[i];
        int pitch = b[0] & 0x7f, oct = pitch / 12 - 3;
        v->octave = (int8_t)(oct < -3 ? -3 : oct > 5 ? 5 : oct);
        v->semi = (uint8_t)(pitch % 12);
        v->fine = b[1] >> 3;
        v->pitch_mod[0].source = src(b[2]);
        v->pitch_mod[0].depth = depth(half(b[3]));
        v->pitch_mod[1].source = src(b[2] >> 4);
        v->pitch_mod[1].depth = depth(half(b[4]));
        v->wave = (uint8_t)(b[5] > 74 ? 74 : b[5]);
        v->level = (b[6] & 0x7f) >> 1;
        v->output = (b[6] & 0x80) != 0;
        v->level_mod[0].source = src(b[7]);
        v->level_mod[0].depth = depth(half(b[8]));
        v->level_mod[1].source = src(b[7] >> 4);
        v->level_mod[1].depth = depth(half(b[9]));
    }
    out->final_level = (p[0x58] & 0x7f) >> 1;
    out->am = (p[0x58] & 0x80) != 0;
    out->cutoff = p[0x59] & 0x7f;
    out->sync = (p[0x59] & 0x80) != 0;
    out->resonance = p[0x5a] & 31;
    out->filter_mod[0].source = src(p[0x5b]);
    out->filter_mod[0].depth = depth(s7(p[0x5c]));
    out->filter_mod[1].source = src(p[0x5b] >> 4);
    out->filter_mod[1].depth = depth(s7(p[0x5d]));
    out->restart_voice = (p[0x5c] & 0x80) != 0;
    out->mono = (p[0x5d] & 0x80) != 0;
    out->key_track = (p[0x5e] & 0x7f) >> 1;
    out->restart_env = (p[0x5e] & 0x80) != 0;
    out->glide = p[0x5f] & 0x7f;
    out->restart_osc = (p[0x5f] & 0x80) != 0;
    out->pan = p[0x64] >> 4;
    out->pan_mod.source = src(p[0x64]);
    out->pan_mod.depth = depth(s7(p[0x65]));
    out->cycle = (p[0x65] & 0x80) != 0;
}

void sq80_default_patch(sq80_patch *out) {
    int i;
    sq80_zero(out, sizeof *out);
    sq80_copy(out->name, "INIT  ", 7);
    for (i = 0; i < 4; ++i) out->env[i].l1 = out->env[i].l2 = out->env[i].l3 = (int8_t)(i == 3 ? 63 : 0);
    for (i = 0; i < 3; ++i) {
        out->lfo[i].mod = SQ80_OFF;
        out->osc[i].pitch_mod[0].source = out->osc[i].pitch_mod[1].source = SQ80_OFF;
        out->osc[i].level_mod[0].source = out->osc[i].level_mod[1].source = SQ80_OFF;
    }
    out->osc[0].level = 63;
    out->osc[0].output = 1;
    out->final_level = 63;
    out->cutoff = 127;
    out->filter_mod[0].source = out->filter_mod[1].source = SQ80_OFF;
    out->pan = 8;
    out->pan_mod.source = SQ80_OFF;
}

/* ------------------------------------------------------------------------- SysEx */

void sq80_sysex_begin(sq80_sysex_reader *r, const uint8_t *data, size_t size) {
    r->data = data;
    r->size = size;
    r->pos = 0;
    r->left = 0;
}

int sq80_sysex_next(sq80_sysex_reader *r, uint8_t out[102]) {
    for (;;) {
        if (r->left > 0) {
            if (r->pos + 204 <= r->size) {
                int nibbles = 1, i;
                for (i = 0; i < 204; ++i)
                    if (r->data[r->pos + i] >= 0x10) { nibbles = 0; break; }
                if (nibbles) {
                    for (i = 0; i < 102; ++i)
                        out[i] = (uint8_t)((r->data[r->pos + 2 * i] & 15) | ((r->data[r->pos + 2 * i + 1] & 15) << 4));
                    r->pos += 204;
                    --r->left;
                    return 1;
                }
            }
            r->left = 0;   /* truncated or malformed: look for the next message */
        }
        while (r->pos + 5 <= r->size) {
            const uint8_t *h = r->data + r->pos;
            if (h[0] == 0xf0 && h[1] == 0x0f && h[2] == 0x02 && (h[4] == 0x01 || h[4] == 0x02)) {
                r->left = h[4] == 0x01 ? 1 : 40;
                r->pos += 5;
                break;
            }
            ++r->pos;
        }
        if (r->left == 0) return 0;
    }
}
