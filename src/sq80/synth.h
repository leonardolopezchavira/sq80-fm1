// SQ-80 voice engine: portable C++17, single-precision float, no allocation while rendering.
//
// Per voice: three DOC 5503-style wavetable oscillators running at the DOC's own rate
// (38455.855 Hz) and resampled to the host rate, a 4-pole resonant low-pass filter, the final
// DCA with pan, four envelopes and three LFOs updated at the SQ-80's control rate (83.59 Hz).
#pragma once

#include <cstdint>

#include "patch.h"
#include "rompack.h"

namespace sq80 {

class Envelope {
public:
    void start(const EnvParams& p, int key, int velocity, bool cycle, bool keepLevel);
    void release();
    int tick();               // advances one control tick, returns the level (-128..126)
    bool active() const { return active_; }
    int level() const { return output_; }

private:
    void segment(int target, int time);
    void next();
    void beginRelease();
    enum Stage : uint8_t { kIdle, kAttack, kDecay1, kDecay2, kSustain, kRelease, kRelease2, kDone };
    EnvParams p_{};
    int levels_[4] = {};      // L0..L3, -128..126
    int scale_ = 256;         // velocity level scaling, 256 = unity
    int tkOffset_ = 0;        // TK, subtracted from T2/T3
    int t1Offset_ = 0;        // T1V
    int value_ = 0;           // 8.8
    int target_ = 0;
    int step_ = 0;
    int ticks_ = 0;
    int output_ = 0;
    Stage stage_ = kIdle;
    bool cycle_ = false, released_ = false, active_ = false;
};

class Lfo {
public:
    void start(const LfoParams& p, bool firstNote);
    int tick(int modSource);  // one control tick, returns -127..127 scaled by the level
    int output() const { return output_; }

private:
    LfoParams p_{};
    uint32_t phase_ = 0;
    int level_ = 0;           // current level, 0..126 (L*2)
    int levelTarget_ = 0;
    int emuClock_ = 0;
    int output_ = 0;
    uint32_t noise_ = 0x12345678u;
    int noiseHeld_ = 0;
    int humanOffset_ = 0;
};

struct Oscillator {
    uint32_t phase = 0;       // one table traversal = 2^32
    uint32_t inc = 0;
    uint32_t start = 0;
    uint8_t shift = 24;       // 32 - log2(table length)
    bool halted = true;
    bool oneShot = false;
    bool wrapped = false;
    int rawIndex = -1;
    float amp = 0.0f;         // current amplitude (smoothed toward ampTarget)
    float ampTarget = 0.0f;
};

class Filter {
public:
    void reset() { s_[0] = s_[1] = s_[2] = s_[3] = 0.0f; }
    void set(float cutoffHz, float resonance, float sampleRate);
    float process(float x);

private:
    float g_ = 0.5f, k_ = 0.0f;
    float s_[4] = {};
};

struct Voice {
    bool active = false;
    bool released = false;
    int key = 60, velocity = 100;
    uint32_t age = 0;
    float pitchKey = 60.0f;   // with glide
    float glideFrom = 60.0f, glideRate = 0.0f;
    Oscillator osc[3];
    Envelope env[4];
    Lfo lfo[3];
    Filter filter;
    // DOC-rate stream and its resampler
    float hist[4] = {};
    float docPhase = 0.0f;
    // control-rate targets, ramped per sample
    float gainL = 0.0f, gainR = 0.0f, gainLTarget = 0.0f, gainRTarget = 0.0f;
    float cutoff = 1000.0f, cutoffTarget = 1000.0f;
};

class Synth {
public:
    static constexpr int kVoices = 8;

    // `rom` must stay valid (on the FM-1 it lives in flash).
    explicit Synth(const RomPack* rom, float sampleRate = 44100.0f);

    void setPatch(const Patch& p);
    const Patch& patch() const { return patch_; }

    void noteOn(int key, int velocity);
    void noteOff(int key);
    void controlChange(int cc, int value);
    void pitchBend(int value14);       // 0..16383, 8192 = centre
    void channelPressure(int value);
    void allNotesOff();

    // Renders `n` stereo frames, adding nothing: the buffers are overwritten.
    void render(float* left, float* right, int n);

    int activeVoices() const;

private:
    void startVoice(Voice& v, int key, int velocity, bool legato);
    void controlTick(Voice& v);
    float renderDoc(Voice& v);
    void setupOscillator(Voice& v, int i, float pitch);
    int source(const Voice& v, uint8_t s) const;

    const RomPack* rom_;
    Patch patch_{};
    Voice voices_[kVoices];
    float sampleRate_;
    float docStep_;           // DOC samples per host sample
    float controlCountdown_ = 0.0f;
    float samplesPerTick_;
    uint32_t ageCounter_ = 0;
    int heldKeys_[16] = {};
    int heldCount_ = 0;
    int lastKey_ = -1;
    // MIDI state
    int wheel_ = 0, pedal_ = 0, xctrl_ = 0, pressure_ = 0, volume_ = 127, sustain_ = 0;
    float bendSemis_ = 0.0f;
    bool lfoStarted_[3] = {};
};

}  // namespace sq80
