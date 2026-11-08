#include "patch.h"

#include <algorithm>
#include <cstring>

namespace sq80 {

namespace {

int s8(uint8_t b) { return b >= 128 ? b - 256 : b; }
int s7(uint8_t b) { b &= 0x7f; return b >= 64 ? b - 128 : b; }
int half(uint8_t b) { return s8(b) / 2; }  // C++ division rounds toward zero, as the SQ-80 does
int8_t depth(int v) { return static_cast<int8_t>(std::clamp(v, -63, 63)); }
uint8_t src(int v) { return static_cast<uint8_t>(v & 15); }

}  // namespace

void decodePatch(const uint8_t p[102], Patch& out) {
    out = Patch{};
    for (int i = 0; i < 6; ++i) out.name[i] = (p[i] >= 0x20 && p[i] < 0x7f) ? static_cast<char>(p[i]) : ' ';
    out.name[6] = 0;

    for (int e = 0; e < 4; ++e) {
        const uint8_t* b = p + 6 + 10 * e;
        EnvParams& v = out.env[e];
        v.l1 = depth(half(b[0]));
        v.l2 = depth(half(b[1]));
        v.l3 = depth(half(b[2]));
        v.t1 = b[3] & 63;
        v.t2 = b[4] & 63;
        v.t3 = b[5] & 63;
        v.t4 = b[6] & 63;
        v.secondRelease = (b[6] & 0x80) != 0;
        v.lv = b[7] >> 2;
        v.t1v = b[8] & 63;
        v.tk = b[9] & 63;
    }
    for (int l = 0; l < 3; ++l) {
        const uint8_t* b = p + 0x2e + 4 * l;
        LfoParams& v = out.lfo[l];
        v.freq = b[0] & 63;
        v.wave = b[0] >> 6;
        v.l1 = b[1] & 63;
        v.l2 = b[2] & 63;
        v.mod = src(((b[1] >> 6) << 2) | (b[2] >> 6));
        v.delay = b[3] & 63;
        v.humanize = (b[3] & 0x40) != 0;
        v.reset = (b[3] & 0x80) != 0;
    }
    for (int o = 0; o < 3; ++o) {
        const uint8_t* b = p + 0x3a + 10 * o;
        OscParams& v = out.osc[o];
        const int pitch = b[0] & 0x7f;
        v.octave = static_cast<int8_t>(std::clamp(pitch / 12 - 3, -3, 5));
        v.semi = static_cast<uint8_t>(pitch % 12);
        v.fine = b[1] >> 3;
        v.pitchMod[0] = {src(b[2]), depth(half(b[3]))};
        v.pitchMod[1] = {src(b[2] >> 4), depth(half(b[4]))};
        v.wave = static_cast<uint8_t>(std::min<int>(b[5], 74));
        v.level = (b[6] & 0x7f) >> 1;
        v.output = (b[6] & 0x80) != 0;
        v.levelMod[0] = {src(b[7]), depth(half(b[8]))};
        v.levelMod[1] = {src(b[7] >> 4), depth(half(b[9]))};
    }
    out.finalLevel = (p[0x58] & 0x7f) >> 1;
    out.am = (p[0x58] & 0x80) != 0;
    out.cutoff = p[0x59] & 0x7f;
    out.sync = (p[0x59] & 0x80) != 0;
    out.resonance = p[0x5a] & 31;
    out.filterMod[0] = {src(p[0x5b]), depth(s7(p[0x5c]))};
    out.filterMod[1] = {src(p[0x5b] >> 4), depth(s7(p[0x5d]))};
    out.restartVoice = (p[0x5c] & 0x80) != 0;
    out.mono = (p[0x5d] & 0x80) != 0;
    out.keyTrack = (p[0x5e] & 0x7f) >> 1;
    out.restartEnv = (p[0x5e] & 0x80) != 0;
    out.glide = p[0x5f] & 0x7f;
    out.restartOsc = (p[0x5f] & 0x80) != 0;
    out.pan = p[0x64] >> 4;
    out.panMod = {src(p[0x64]), depth(s7(p[0x65]))};
    out.cycle = (p[0x65] & 0x80) != 0;
}

bool SysexReader::next(uint8_t out[102]) {
    for (;;) {
        if (left_ > 0) {
            if (pos_ + 204 <= size_) {
                bool nibbles = true;
                for (size_t i = 0; i < 204; ++i)
                    if (data_[pos_ + i] >= 0x10) { nibbles = false; break; }
                if (nibbles) {
                    for (int i = 0; i < 102; ++i)
                        out[i] = static_cast<uint8_t>((data_[pos_ + 2 * i] & 15) |
                                                      ((data_[pos_ + 2 * i + 1] & 15) << 4));
                    pos_ += 204;
                    --left_;
                    return true;
                }
            }
            left_ = 0;  // truncated or malformed message: look for the next one
        }
        // find the next ESQ-1/SQ-80 program or bank dump header
        while (pos_ + 5 <= size_) {
            const uint8_t* h = data_ + pos_;
            if (h[0] == 0xf0 && h[1] == 0x0f && h[2] == 0x02 && (h[4] == 0x01 || h[4] == 0x02)) {
                left_ = h[4] == 0x01 ? 1 : 40;
                pos_ += 5;
                break;
            }
            ++pos_;
        }
        if (left_ == 0) return false;
    }
}

}  // namespace sq80
