// Engine tests that need no Ensoniq data (synthetic ROM pack). Run: ctest or ./sq80_tests
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "sq80/sq80.h"
#include "sq80/sq80_cal.h"
#include "synthetic_rom.h"

static int failures = 0;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            ++failures;                                                    \
            std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);    \
            std::printf(__VA_ARGS__);                                      \
            std::printf("\n");                                             \
        }                                                                  \
    } while (0)

// A neutral program as SQ-80 bytes: OSC1 at full level on `wave`, filter open, ENV4 full.
static void basicProgram(uint8_t p[102], int wave) {
    std::memset(p, 0, 102);
    std::memcpy(p, "TEST  ", 6);
    for (int k = 0; k < 3; ++k) p[6 + 30 + k] = 126;  // ENV4 L1..L3 = 63
    for (int l = 0; l < 3; ++l) { p[0x2e + 4 * l + 1] = 0xc0; p[0x2e + 4 * l + 2] = 0xc0; }  // LFO MOD OFF
    for (int o = 0; o < 3; ++o) {
        uint8_t* b = p + 0x3a + 10 * o;
        b[0] = 36;            // octave 0, semitone 0
        b[2] = 0xff;          // pitch mods OFF
        b[5] = static_cast<uint8_t>(wave);
        b[6] = o == 0 ? (0x80 | 126) : 0;  // DCA1 on at 63
        b[7] = 0xff;          // level mods OFF
    }
    p[0x58] = 126;            // DCA4 63
    p[0x59] = 127;            // filter open
    p[0x5b] = 0xff;           // filter mods OFF
    p[0x64] = 0x8f;           // pan 8, pan mod OFF
}

static std::vector<float> renderNote(sq80_synth& s, int key, float seconds, float hold) {
    const int n = static_cast<int>(44100 * seconds), off = static_cast<int>(44100 * hold);
    std::vector<float> l(n), r(n);
    sq80_note_on(&s, key, 100);
    for (int pos = 0; pos < n; pos += 32) {
        if (pos <= off && off < pos + 32) sq80_note_off(&s, key);
        sq80_render(&s, &l[pos], &r[pos], std::min(32, n - pos));
    }
    return l;
}

static double estimateHz(const std::vector<float>& x, int from, int to) {
    int crossings = 0, first = -1, last = -1;
    for (int i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            if (first < 0) first = i;
            last = i;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * 44100.0 / (last - first) : 0.0;
}

int main() {
    const std::vector<uint8_t> pack = sq80test::makeSyntheticRomPack();
    sq80_rompack rom;
    CHECK(sq80_rompack_load(&rom, pack.data(), pack.size()), "synthetic pack rejected");
    {
        std::vector<uint8_t> bad = pack;
        bad[0] = 'X';
        sq80_rompack r2;
        CHECK(!sq80_rompack_load(&r2, bad.data(), bad.size()), "bad magic accepted");
        CHECK(!sq80_rompack_load(&r2, pack.data(), 100), "truncated pack accepted");
    }

    // ---- patch decoding
    uint8_t prog[102];
    basicProgram(prog, 2);
    prog[0x3a] = 12 * 4 + 7;   // OSC1 octave +1, semitone 7
    prog[0x3b] = 21 << 3;      // fine 21
    prog[6 + 0] = 0x82;        // ENV1 L1 = -63
    prog[0x2e] = 20 | (1 << 6);// LFO1 FREQ 20, SAW
    prog[0x2f] = 5 | (2 << 6); // L1 5, MOD bits 3-2 = 2
    prog[0x30] = 7 | (3 << 6); // L2 7, MOD bits 1-0 = 3 -> MOD 11 = WHEEL
    prog[0x5c] = 0x80 | 0x7e;  // filter MOD1 depth -2, RESTART VC
    sq80_patch p;
    sq80_decode_patch(prog, &p);
    CHECK(std::strcmp(p.name, "TEST  ") == 0, "name '%s'", p.name);
    CHECK(p.osc[0].octave == 1 && p.osc[0].semi == 7 && p.osc[0].fine == 21, "osc pitch %d %d %d",
          p.osc[0].octave, p.osc[0].semi, p.osc[0].fine);
    CHECK(p.env[0].l1 == -63 && p.env[3].l1 == 63, "env levels %d %d", p.env[0].l1, p.env[3].l1);
    CHECK(p.lfo[0].freq == 20 && p.lfo[0].wave == 1 && p.lfo[0].l1 == 5 && p.lfo[0].l2 == 7 &&
          p.lfo[0].mod == SQ80_WHEEL, "lfo %d %d %d %d %d", p.lfo[0].freq, p.lfo[0].wave, p.lfo[0].l1,
          p.lfo[0].l2, p.lfo[0].mod);
    CHECK(p.filter_mod[0].depth == -2 && p.restart_voice, "filter mod %d", p.filter_mod[0].depth);
    CHECK(p.osc[0].output && p.osc[0].level == 63 && !p.osc[1].output, "dca");

    // ---- SysEx: a bank of 40 between junk, and a single program
    {
        std::vector<uint8_t> syx = {0x12, 0xf0, 0x43, 0x00, 0xf7};  // junk + a foreign message
        syx.insert(syx.end(), {0xf0, 0x0f, 0x02, 0x00, 0x02});
        for (int n = 0; n < 40; ++n) {
            uint8_t q[102];
            basicProgram(q, n % 75);
            q[0] = static_cast<uint8_t>('A' + n % 26);
            for (uint8_t b : q) { syx.push_back(b & 15); syx.push_back(b >> 4); }
        }
        syx.push_back(0xf7);
        syx.insert(syx.end(), {0xf0, 0x0f, 0x02, 0x05, 0x01});
        for (uint8_t b : prog) { syx.push_back(b & 15); syx.push_back(b >> 4); }
        syx.push_back(0xf7);
        sq80_sysex_reader reader;
        sq80_sysex_begin(&reader, syx.data(), syx.size());
        uint8_t q[102];
        int count = 0;
        bool firstOk = false, lastOk = false;
        while (sq80_sysex_next(&reader, q)) {
            if (count == 0) firstOk = q[0] == 'A';
            lastOk = std::memcmp(q, prog, 102) == 0;
            ++count;
        }
        CHECK(count == 41, "read %d programs", count);
        CHECK(firstOk && lastOk, "program contents");
    }

    // ---- pitch: wave 0 (synthetic sine) at key 69 and 81
    {
        basicProgram(prog, 0);
        sq80_decode_patch(prog, &p);
        static sq80_synth s;
        sq80_init(&s, &rom, 44100.0f);
        sq80_set_patch(&s, &p);
        std::vector<float> x = renderNote(s, 69, 1.0f, 2.0f);
        const double hz = estimateHz(x, 4410, 44100);
        CHECK(std::fabs(hz - 440.0) < 0.5, "A4 = %.2f Hz", hz);
        sq80_all_notes_off(&s);
        static sq80_synth s2;
        sq80_init(&s2, &rom, 44100.0f);
        prog[0x3a] = 12 * 4;  // octave +1
        sq80_decode_patch(prog, &p);
        sq80_set_patch(&s2, &p);
        x = renderNote(s2, 69, 1.0f, 2.0f);
        const double hz2 = estimateHz(x, 4410, 44100);
        CHECK(std::fabs(hz2 - 880.0) < 1.0, "A5 (octave +1) = %.2f Hz", hz2);
    }

    // ---- envelope: ENV4 63 -> 0 in T2 = 39 lasts kEnvTicks[39] control ticks
    {
        basicProgram(prog, 0);
        prog[6 + 30 + 1] = 0;  // ENV4 L2 = 0
        prog[6 + 30 + 2] = 0;  // L3 = 0
        prog[6 + 30 + 4] = 39; // T2
        sq80_decode_patch(prog, &p);
        static sq80_synth s;
        sq80_init(&s, &rom, 44100.0f);
        sq80_set_patch(&s, &p);
        std::vector<float> x = renderNote(s, 69, 3.0f, 3.0f);
        int lastSound = 0;
        for (int i = 0; i < static_cast<int>(x.size()); ++i)
            if (std::fabs(x[i]) > 1e-4f) lastSound = i;
        const double secs = lastSound / 44100.0;
        const double want = SQ80_ENV_TICKS[39] / SQ80_CONTROL_RATE;
        CHECK(std::fabs(secs - want) < 0.05, "T2=39 lasted %.3f s, want %.3f s", secs, want);
        CHECK(sq80_active_voices(&s) == 0, "voice still active after its envelope ended");
    }

    // ---- release, polyphony, one-shot, stability
    {
        basicProgram(prog, 1);
        prog[6 + 30 + 6] = 20;  // ENV4 T4
        sq80_decode_patch(prog, &p);
        static sq80_synth s;
        sq80_init(&s, &rom, 44100.0f);
        sq80_set_patch(&s, &p);
        for (int k = 0; k < 10; ++k) sq80_note_on(&s, 48 + k, 100);
        std::vector<float> l(4410), r(4410);
        sq80_render(&s, l.data(), r.data(), 4410);
        CHECK(sq80_active_voices(&s) == SQ80_VOICES, "%d voices", sq80_active_voices(&s));
        for (int k = 0; k < 10; ++k) sq80_note_off(&s, 48 + k);
        bool finite = true;
        for (int i = 0; i < 50; ++i) {
            sq80_render(&s, l.data(), r.data(), 4410);
            for (float v : l) finite = finite && std::isfinite(v) && std::fabs(v) < 8 * 3 * 2 * SQ80_OUTPUT_GAIN;
        }
        CHECK(finite, "non-finite or runaway output");
        CHECK(sq80_active_voices(&s) == 0, "%d voices left after release", sq80_active_voices(&s));
        float tail = 0.0f;
        for (float v : l) tail = std::max(tail, std::fabs(v));
        CHECK(tail == 0.0f, "not silent after release (%g)", tail);
    }
    {
        basicProgram(prog, 60);  // one-shot wave: stops at its zero byte
        sq80_decode_patch(prog, &p);
        static sq80_synth s;
        sq80_init(&s, &rom, 44100.0f);
        sq80_set_patch(&s, &p);
        std::vector<float> x = renderNote(s, 60, 2.0f, 2.0f);
        float late = 0.0f;
        for (int i = 44100; i < 88200; ++i) late = std::max(late, std::fabs(x[i]));
        CHECK(late < 1e-3f, "one-shot still sounding (%g)", late);
    }

    {
        // the built-in test pack (the FM-1 test firmware's): every wave it maps sounds, the one-shot stops
        static uint8_t buf[SQ80_TEST_PACK_SIZE];
        sq80_rompack tp;
        CHECK(sq80_rompack_load(&tp, buf, sq80_test_pack(buf)), "test pack rejected");
        const int waves[] = {0, 2, 3, 30, 60};
        for (int w : waves) {
            basicProgram(prog, w);
            sq80_decode_patch(prog, &p);
            static sq80_synth s;
            sq80_init(&s, &tp, 44100.0f);
            sq80_set_patch(&s, &p);
            std::vector<float> x = renderNote(s, 60, 0.3f, 0.3f);
            float peak = 0.0f;
            for (int i = 0; i < 4410; ++i) peak = std::max(peak, std::fabs(x[i]));
            CHECK(peak > 0.05f, "test pack wave %d silent (peak %g)", w, peak);
        }
    }

    std::printf(failures ? "%d FAILED\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
