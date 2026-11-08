#include "synth.h"

#include <algorithm>
#include <cmath>

#include "calibration.h"

namespace sq80 {

namespace {

constexpr float kPi = 3.14159265358979f;

int levelBytes(int c) { return c >= 0 ? 2 * c : 2 * (c - 1); }  // -63..63 -> -128..126

float semitoneRatio(float semis) { return std::exp2(semis * (1.0f / 12.0f)); }

float dcaGain(float level) {  // level 0..63 (may be fractional / out of range)
    if (level <= 0.0f) return 0.0f;
    if (level >= 63.0f) return 1.0f;
    int i = 1;
    while (level > cal::kDcaLevel[i]) ++i;
    const float t = (level - cal::kDcaLevel[i - 1]) / (cal::kDcaLevel[i] - cal::kDcaLevel[i - 1]);
    const float db = cal::kDcaDb[i - 1] + (cal::kDcaDb[i] - cal::kDcaDb[i - 1]) * t;
    return std::exp2(db * (3.32192809f / 20.0f));
}

// FILTER FREQ steps (may be fractional or out of 0..127) to the cutoff parameter in Hz.
float cutoffHz(float steps) {
    if (steps <= 0.0f) return cal::kCutoffHz[0] * std::exp2(steps * (1.0f / 12.0f));
    if (steps >= 127.0f) return cal::kCutoffHz[127];
    const int i = static_cast<int>(steps);
    const float t = steps - static_cast<float>(i);
    return cal::kCutoffHz[i] + (cal::kCutoffHz[i + 1] - cal::kCutoffHz[i]) * t;
}

}  // namespace

// ------------------------------------------------------------------------------- Envelope

void Envelope::start(const EnvParams& p, int key, int velocity, bool cycle, bool keepLevel) {
    p_ = p;
    levels_[0] = 0;
    levels_[1] = levelBytes(p.l1);
    levels_[2] = levelBytes(p.l2);
    levels_[3] = levelBytes(p.l3);
    velocity = std::clamp(velocity, 0, 127);
    // velocity level scaling: LV 0 = none (252/256), LV 63 = fully velocity dependent
    scale_ = ((p.lv * velocity) >> 5) + (63 - p.lv) * 4;
    tkOffset_ = p.tk * std::max(0, 2 * (key - 36)) / 256;
    t1Offset_ = (p.t1v * velocity) >> 7;
    cycle_ = cycle;
    released_ = false;
    active_ = true;
    if (!keepLevel) value_ = 0;
    stage_ = kAttack;
    segment(levels_[1] * scale_ / 256, p.t1 - t1Offset_);
}

void Envelope::segment(int target, int time) {
    time = std::clamp(time, 0, 63);
    target_ = target;
    ticks_ = std::max<int>(1, cal::kEnvTicks[time]);
    step_ = (std::abs(target * 256 - value_)) / ticks_;
}

void Envelope::next() {
    switch (stage_) {
    case kAttack:
        stage_ = kDecay1;
        segment(levels_[2] * scale_ / 256, p_.t2 - tkOffset_);
        break;
    case kDecay1:
        stage_ = kDecay2;
        segment(levels_[3] * scale_ / 256, p_.t3 - tkOffset_);
        break;
    case kDecay2:
        if (value_ == 0) stage_ = kDone;
        else if (cycle_) beginRelease();  // CYC (full cycle): no sustain
        else stage_ = kSustain;
        break;
    case kRelease:
        stage_ = kRelease2;
        segment(0, cal::kSecondReleaseTime);
        break;
    case kRelease2:
        stage_ = kDone;
        break;
    default:
        break;
    }
}

void Envelope::beginRelease() {
    if (p_.secondRelease) {  // "R": half the level minus 12 in T4, then to 0 in time 39
        stage_ = kRelease;
        segment(value_ / 512 - 12, p_.t4);
    } else {
        stage_ = kRelease2;
        segment(0, p_.t4);
    }
}

void Envelope::release() {
    if (!active_ || cycle_ || released_) return;  // a full-cycle envelope ignores the key
    released_ = true;
    if (stage_ == kRelease || stage_ == kRelease2 || stage_ == kDone) return;
    beginRelease();
}

int Envelope::tick() {
    if (!active_) return output_ = 0;
    if (stage_ == kDone) {
        active_ = false;
        value_ = 0;
        return output_ = 0;
    }
    if (stage_ != kSustain) {
        const int goal = target_ * 256;
        if (value_ < goal) value_ = std::min(goal, value_ + step_);
        else if (value_ > goal) value_ = std::max(goal, value_ - step_);
        if (--ticks_ <= 0) {
            value_ = goal;
            next();
        }
    }
    output_ = value_ / 256;
    return output_;
}

// ------------------------------------------------------------------------------------ LFO

void Lfo::start(const LfoParams& p, bool firstNote) {
    p_ = p;
    if (p.reset || firstNote) phase_ = 0;
    level_ = p.l1 * 2;
    levelTarget_ = p.l2 * 2;
    emuClock_ = 0;
}

int Lfo::tick(int modSource) {
    // rate: linear in FREQ, 0.33 Hz at 7 and 18.6 Hz at 63 (83.59 Hz control rate)
    const int f = (p_.freq << 8) + humanOffset_;
    uint32_t inc;
    if (f < 0x700) inc = static_cast<uint32_t>((static_cast<int64_t>(f) << 16) / 7);
    else inc = static_cast<uint32_t>(static_cast<int64_t>(f - 0x600) << 16);
    const uint32_t before = phase_;
    phase_ += inc;  // one cycle = 2^32

    const int b = static_cast<int>(phase_ >> 24);
    int v;
    switch (p_.wave) {
    case 0: v = ((b < 0x80 ? b : (~b & 0xff)) - 0x40) * 2; break;                    // TRI
    case 1: v = static_cast<int8_t>(static_cast<uint8_t>(b + 0x80)); break;            // SAW
    case 2: v = b < 0x80 ? 127 : 0; break;                                            // SQR (unipolar)
    default:                                                                          // NOISE
        if ((phase_ ^ before) & 0x80000000u || inc >= 0x80000000u) {
            noise_ = noise_ * 1664525u + 1013904223u;
            noiseHeld_ = static_cast<int8_t>(noise_ >> 24);
        }
        v = noiseHeld_;
        break;
    }

    // delayed fade from L1 to L2: one step of DELAY every 7.125 ticks (the SQ-80's LFO update)
    emuClock_ -= 1024;
    if (emuClock_ <= 0) {
        emuClock_ += 7296;
        if (level_ < levelTarget_) level_ = std::min(levelTarget_, level_ + p_.delay);
        else if (level_ > levelTarget_) level_ = std::max(levelTarget_, level_ - p_.delay);
        if (p_.humanize && p_.freq >= 7) {
            noise_ = noise_ * 1664525u + 1013904223u;
            humanOffset_ = static_cast<int>((noise_ >> 30) % 3) * 256 / 2;  // 0..+1 FREQ units
        }
    }
    const int amp = level_ + std::max(0, modSource);
    output_ = amp <= 0 ? 0 : std::clamp(v * amp / 256, -127, 127);
    return output_;
}

// --------------------------------------------------------------------------------- Filter

void Filter::set(float cutoffHz, float resonance, float sampleRate) {
    cutoffHz = std::clamp(cutoffHz, 10.0f, sampleRate * 0.45f);
    g_ = std::tan(kPi * cutoffHz / sampleRate);
    k_ = resonance;
}

float Filter::process(float x) {
    // 4-pole zero-delay-feedback ladder, input saturation like the CEM3379's
    const float G = g_ / (1.0f + g_);
    const float inv = 1.0f / (1.0f + g_);
    const float S = ((G * G * G * s_[0] + G * G * s_[1] + G * s_[2] + s_[3]) * inv);
    const float G4 = G * G * G * G;
    const float y4 = (G4 * (1.0f + cal::kFilterBassCompensation * k_) * x + S) / (1.0f + k_ * G4);
    float u = std::tanh(cal::kFilterDrive * ((1.0f + cal::kFilterBassCompensation * k_) * x - k_ * y4)) *
              (1.0f / cal::kFilterDrive);
    for (int i = 0; i < 4; ++i) {
        const float v = (u - s_[i]) * G;
        const float y = v + s_[i];
        s_[i] = y + v;
        u = y;
    }
    return u;
}

// ---------------------------------------------------------------------------------- Synth

Synth::Synth(const RomPack* rom, float sampleRate) : rom_(rom), sampleRate_(sampleRate) {
    docStep_ = cal::kDocRate / sampleRate;
    samplesPerTick_ = sampleRate / cal::kControlRate;
    Patch p;
    p.osc[0].output = true;
    p.osc[0].level = 63;
    p.env[3].l1 = p.env[3].l2 = p.env[3].l3 = 63;
    setPatch(p);
}

void Synth::setPatch(const Patch& p) {
    patch_ = p;
}

int Synth::activeVoices() const {
    int n = 0;
    for (const Voice& v : voices_) n += v.active ? 1 : 0;
    return n;
}

int Synth::source(const Voice& v, uint8_t s) const {
    switch (s) {
    case kLfo1: case kLfo2: case kLfo3: return v.lfo[s - kLfo1].output();
    case kEnv1: case kEnv2: case kEnv3: case kEnv4: return v.env[s - kEnv1].level();
    case kVel: return v.velocity;
    case kVelX: return v.velocity * v.velocity / 127;
    case kKbd: return v.key;
    case kKbd2: return v.key <= 32 ? -128 : v.key >= 95 ? 124 : (v.key - 64) * 4;
    case kWheel: return wheel_;
    case kPedal: return pedal_;
    case kXctrl: return xctrl_;
    case kPress: return pressure_;
    default: return 0;
    }
}

void Synth::setupOscillator(Voice& v, int i, float pitch) {
    const OscParams& op = patch_.osc[i];
    Oscillator& o = v.osc[i];
    const int zoneKey = std::clamp(static_cast<int>(v.key) + op.octave * 12 + op.semi, 0, 127);
    const int raw = rom_->rawIndex(op.wave, zoneKey >> 3);
    const RawWave w = rom_->raw(raw);
    if (raw != o.rawIndex) {
        o.rawIndex = raw;
        o.start = w.start;
        o.shift = static_cast<uint8_t>(32 - w.sizeLog2);
    }
    o.oneShot = op.wave >= 54;
    const float semis = pitch + w.tuneSemi + w.tuneFrac * (1.0f / 256.0f) - cal::kA4Key;
    // DOC: one table pass takes 2^(17 + resolution) accumulator steps, so each resolution step
    // halves the rate. Single-cycle waves (resolution 1) are the reference.
    const float hz = cal::kA4 * semitoneRatio(semis) * std::exp2(1.0f - static_cast<float>(w.resolution));
    const double inc = static_cast<double>(hz) / cal::kDocRate * 4294967296.0;
    o.inc = static_cast<uint32_t>(std::min(inc, 2147483647.0));
}

void Synth::startVoice(Voice& v, int key, int velocity, bool legato) {
    const bool reuse = v.active;
    v.key = key;
    v.velocity = velocity;
    v.age = ++ageCounter_;
    // glide from the last key played
    const float glideTicks = patch_.glide > 0 ? cal::kEnvTicks[std::min<int>(patch_.glide, 63)] : 0.0f;
    if (glideTicks > 0 && lastKey_ >= 0) {
        v.pitchKey = reuse ? v.pitchKey : static_cast<float>(lastKey_);
        v.glideRate = std::abs(key - v.pitchKey) / glideTicks;
    } else {
        v.pitchKey = static_cast<float>(key);
        v.glideRate = 0.0f;
    }
    if (legato) return;

    v.active = true;
    v.released = false;
    const bool keepEnv = reuse && !patch_.restartEnv;
    for (int e = 0; e < 4; ++e) v.env[e].start(patch_.env[e], key, velocity, patch_.cycle, keepEnv);
    for (int l = 0; l < 3; ++l) {
        v.lfo[l].start(patch_.lfo[l], !lfoStarted_[l]);
        lfoStarted_[l] = true;
    }
    for (int i = 0; i < 3; ++i) {
        Oscillator& o = v.osc[i];
        o.rawIndex = -1;
        setupOscillator(v, i, v.pitchKey + patch_.osc[i].octave * 12 + patch_.osc[i].semi);
        if (!reuse || patch_.restartOsc || o.oneShot) o.phase = 0;
        o.halted = false;
        if (!reuse) o.amp = 0.0f;
    }
    if (!reuse) {
        v.filter.reset();
        for (float& h : v.hist) h = 0.0f;
        v.docPhase = 0.0f;
        v.gainL = v.gainR = 0.0f;
    }
    controlTick(v);
    if (!reuse) v.cutoff = v.cutoffTarget;
}

void Synth::noteOn(int key, int velocity) {
    if (velocity <= 0) { noteOff(key); return; }
    key = std::clamp(key, 0, 127);
    velocity = std::clamp(velocity, 1, 127);
    if (heldCount_ < 16) heldKeys_[heldCount_++] = key;

    if (patch_.mono) {
        Voice& v = voices_[0];
        for (int i = 1; i < kVoices; ++i) voices_[i].active = false;
        const bool legato = v.active && !v.released && heldCount_ > 1;
        startVoice(v, key, velocity, legato);
        lastKey_ = key;
        return;
    }

    Voice* target = nullptr;
    if (patch_.restartVoice)
        for (Voice& v : voices_)
            if (v.active && v.key == key) { target = &v; break; }
    if (!target)
        for (Voice& v : voices_)
            if (!v.active) { target = &v; break; }
    if (!target) {  // steal: the oldest released voice, else the oldest
        for (Voice& v : voices_)
            if (v.released && (!target || v.age < target->age)) target = &v;
        if (!target)
            for (Voice& v : voices_)
                if (!target || v.age < target->age) target = &v;
        target->active = false;  // a stolen voice starts fresh
    }
    startVoice(*target, key, velocity, false);
    lastKey_ = key;
}

void Synth::noteOff(int key) {
    for (int i = 0; i < heldCount_; ++i)
        if (heldKeys_[i] == key) {
            for (int j = i; j + 1 < heldCount_; ++j) heldKeys_[j] = heldKeys_[j + 1];
            --heldCount_;
            break;
        }
    if (patch_.mono) {
        Voice& v = voices_[0];
        if (v.active && v.key == key && heldCount_ > 0) {  // back to the newest held key
            startVoice(v, heldKeys_[heldCount_ - 1], v.velocity, true);
            return;
        }
    }
    if (sustain_ >= 64) {
        for (Voice& v : voices_)
            if (v.active && v.key == key) v.released = true;  // released when the pedal goes up
        return;
    }
    for (Voice& v : voices_)
        if (v.active && v.key == key && !v.released) {
            v.released = true;
            for (Envelope& e : v.env) e.release();
        }
}

void Synth::controlChange(int cc, int value) {
    value = std::clamp(value, 0, 127);
    switch (cc) {
    case 1: wheel_ = value; break;
    case 2: xctrl_ = value; break;
    case 4: pedal_ = value; break;
    case 7: volume_ = value; break;
    case 64: {
        const bool wasDown = sustain_ >= 64;
        sustain_ = value;
        if (wasDown && sustain_ < 64)
            for (Voice& v : voices_)
                if (v.active && v.released)
                    for (Envelope& e : v.env) e.release();
        break;
    }
    case 120: case 123: allNotesOff(); break;
    default: break;
    }
}

void Synth::pitchBend(int value14) {
    bendSemis_ = (std::clamp(value14, 0, 16383) - 8192) / 8192.0f * 2.0f;  // +-2 semitones
}

void Synth::channelPressure(int value) { pressure_ = std::clamp(value, 0, 127); }

void Synth::allNotesOff() {
    heldCount_ = 0;
    for (Voice& v : voices_)
        if (v.active) {
            v.released = true;
            for (Envelope& e : v.env) e.release();
        }
}

void Synth::controlTick(Voice& v) {
    for (Envelope& e : v.env) e.tick();
    for (int l = 0; l < 3; ++l) v.lfo[l].tick(source(v, patch_.lfo[l].mod));
    if (!v.env[3].active()) {
        v.active = false;
        return;
    }
    if (v.glideRate > 0.0f) {
        const float target = static_cast<float>(v.key);
        if (v.pitchKey < target) v.pitchKey = std::min(target, v.pitchKey + v.glideRate);
        else v.pitchKey = std::max(target, v.pitchKey - v.glideRate);
        if (v.pitchKey == target) v.glideRate = 0.0f;
    }
    auto modSum = [&](const ModSlot& a, const ModSlot& b) {
        return (source(v, a.source) * a.depth + source(v, b.source) * b.depth) / 64.0f;
    };
    for (int i = 0; i < 3; ++i) {
        const OscParams& op = patch_.osc[i];
        const float pitch = v.pitchKey + bendSemis_ + op.octave * 12 + op.semi + op.fine / 32.0f +
                            modSum(op.pitchMod[0], op.pitchMod[1]) * cal::kPitchModSemitonesPerUnit;
        setupOscillator(v, i, pitch);
        const float level = op.level + modSum(op.levelMod[0], op.levelMod[1]) * cal::kLevelModPerUnit;
        v.osc[i].ampTarget = op.output || (i == 0 && (patch_.am)) ? dcaGain(level) : 0.0f;
    }
    if (patch_.am) v.osc[0].ampTarget = v.osc[1].ampTarget = dcaGain(63.0f);

    const float cutoffSteps = patch_.cutoff +
                              modSum(patch_.filterMod[0], patch_.filterMod[1]) * cal::kCutoffModPerUnit +
                              patch_.keyTrack / 63.0f * cal::kKeyTrackAtMax * static_cast<float>(v.key - cal::kKeyTrackCenter);
    v.cutoffTarget = cutoffHz(cutoffSteps);
    v.filter.set(v.cutoff, cal::kResonance[patch_.resonance & 31], sampleRate_);

    const float env4 = std::max(0, v.env[3].level()) / 126.0f;
    const float gain = std::pow(env4 * (patch_.finalLevel / 63.0f), cal::kFinalExponent) * (volume_ / 127.0f) *
                       cal::kOutputGain;
    // linear pan law: PAN 8 is the centre, 0 and 15 the sides
    const float panSteps = patch_.pan +
                           source(v, patch_.panMod.source) * patch_.panMod.depth / 64.0f * cal::kPanModPerUnit;
    const float pan = std::clamp(panSteps >= 15.0f ? 1.0f : 0.5f + (panSteps - 8.0f) / 16.0f, 0.0f, 1.0f);
    v.gainLTarget = gain * 2.0f * (1.0f - pan);
    v.gainRTarget = gain * 2.0f * pan;
}

float Synth::renderDoc(Voice& v) {
    const uint8_t* rom = rom_->rom();
    static const float kAmpPole = std::pow(0.01f, 1.0f / (0.002f * cal::kDocRate + 1.0f));
    float sum = 0.0f;
    float s0 = 0.0f;
    for (int i = 0; i < 3; ++i) {
        Oscillator& o = v.osc[i];
        o.amp = o.ampTarget + (o.amp - o.ampTarget) * kAmpPole;
        o.wrapped = false;
        if (o.halted) continue;
        const uint32_t before = o.phase;
        o.phase += o.inc;
        o.wrapped = o.phase < before;
        if (o.wrapped && o.oneShot && !(i == 1 && patch_.sync)) { o.halted = true; continue; }
        if (i == 1 && patch_.sync && v.osc[0].wrapped) o.phase = 0;
        const uint8_t s = rom[(o.start + (o.phase >> o.shift)) & (RomPack::kWaveRomSize - 1)];
        if (s == 0) { o.halted = true; continue; }  // a zero byte stops the DOC oscillator
        const float x = (static_cast<int>(s) - 128) * (1.0f / 127.0f);
        if (i == 0) s0 = x;
        if (i == 0 && patch_.am) continue;  // OSC1 only modulates
        float a = o.amp;
        if (i == 1 && patch_.am) a *= s0 * 0.6f + 0.4f;
        if (patch_.osc[i].output || (i == 1 && patch_.am)) sum += x * a;
    }
    return sum;
}

void Synth::render(float* left, float* right, int n) {
    for (int k = 0; k < n; ++k) {
        controlCountdown_ -= 1.0f;
        const bool tick = controlCountdown_ <= 0.0f;
        if (tick) controlCountdown_ += samplesPerTick_;
        float l = 0.0f, r = 0.0f;
        for (Voice& v : voices_) {
            if (!v.active) continue;
            if (tick) {
                controlTick(v);
                if (!v.active) continue;
            }
            v.docPhase += docStep_;
            while (v.docPhase >= 1.0f) {
                v.docPhase -= 1.0f;
                v.hist[0] = v.hist[1];
                v.hist[1] = v.hist[2];
                v.hist[2] = v.hist[3];
                v.hist[3] = renderDoc(v);
            }
            // cubic Hermite between hist[1] and hist[2]
            const float t = v.docPhase;
            const float c0 = v.hist[1];
            const float c1 = 0.5f * (v.hist[2] - v.hist[0]);
            const float c2 = v.hist[0] - 2.5f * v.hist[1] + 2.0f * v.hist[2] - 0.5f * v.hist[3];
            const float c3 = 0.5f * (v.hist[3] - v.hist[0]) + 1.5f * (v.hist[1] - v.hist[2]);
            const float x = ((c3 * t + c2) * t + c1) * t + c0;

            v.cutoff += (v.cutoffTarget - v.cutoff) * 0.002f;
            if ((k & 15) == 0) v.filter.set(v.cutoff, cal::kResonance[patch_.resonance & 31], sampleRate_);
            const float y = v.filter.process(x);
            v.gainL += (v.gainLTarget - v.gainL) * 0.004f;
            v.gainR += (v.gainRTarget - v.gainR) * 0.004f;
            l += y * v.gainL;
            r += y * v.gainR;
        }
        left[k] = l;
        right[k] = r;
    }
}

}  // namespace sq80
