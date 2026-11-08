// ESQ-1 / SQ-80 sound program: the 102-byte SysEx image and its decoded parameters.
// Byte map: docs/patch-format.md.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sq80 {

// Modulation sources in SQ-80 numbering (the 4-bit codes stored in a program).
enum Source : uint8_t {
    kLfo1, kLfo2, kLfo3, kEnv1, kEnv2, kEnv3, kEnv4, kVel, kVelX, kKbd, kKbd2,
    kWheel, kPedal, kXctrl, kPress, kOff
};

struct ModSlot {
    uint8_t source = kOff;  // Source
    int8_t depth = 0;       // -63..+63
};

struct EnvParams {
    int8_t l1 = 0, l2 = 0, l3 = 0;     // -63..+63
    uint8_t t1 = 0, t2 = 0, t3 = 0, t4 = 0;  // 0..63
    bool secondRelease = false;        // T4 "R"
    uint8_t lv = 0, t1v = 0, tk = 0;   // 0..63
};

struct LfoParams {
    uint8_t freq = 0;   // 0..63
    uint8_t wave = 0;   // 0 TRI, 1 SAW, 2 SQR, 3 NOISE
    uint8_t l1 = 0, l2 = 0, delay = 0;  // 0..63
    uint8_t mod = kOff;
    bool humanize = false, reset = false;
};

struct OscParams {
    int8_t octave = 0;   // -3..+5
    uint8_t semi = 0;    // 0..11
    uint8_t fine = 0;    // 0..31 (1/32 semitone)
    uint8_t wave = 0;    // 0..74
    ModSlot pitchMod[2];
    uint8_t level = 0;   // DCA 0..63
    bool output = false; // DCA on
    ModSlot levelMod[2];
};

struct Patch {
    char name[7] = {};
    EnvParams env[4];
    LfoParams lfo[3];
    OscParams osc[3];
    uint8_t finalLevel = 63;  // DCA4 (ENV4 amount), 0..63
    bool am = false;          // OSC1 amplitude-modulates OSC2
    bool sync = false;        // OSC2 synced to OSC1
    uint8_t cutoff = 127;     // FILTER FREQ 0..127
    uint8_t resonance = 0;    // 0..31
    ModSlot filterMod[2];
    uint8_t keyTrack = 0;     // FILTER KEYBD 0..63
    bool restartVoice = false, mono = false, restartEnv = false, restartOsc = false;
    bool cycle = false;       // ENV CYC (no sustain)
    uint8_t glide = 0;        // 0..127
    uint8_t pan = 8;          // 0..15
    ModSlot panMod;
};

// Decodes a 102-byte program. Out-of-range values are clamped.
void decodePatch(const uint8_t program[102], Patch& out);

// Reads the programs of an ESQ-1/SQ-80 SysEx stream: single-program (F0 0F 02 ch 01) and
// 40-program bank (F0 0F 02 ch 02) dumps, two nibbles per byte, low nibble first. Anything
// else in the stream (other messages, stray bytes) is skipped.
class SysexReader {
public:
    SysexReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    // Copies the next program into `out`; false at the end of the stream.
    bool next(uint8_t out[102]);

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    int left_ = 0;  // programs left in the current message
};

}  // namespace sq80
