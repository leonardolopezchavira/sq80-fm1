// SQ-80 ROM pack: the wave directory (from the OS ROM) and the 256 KiB wave ROM, built from
// the user's own dumps by tools/make_rompack.py. Read in place (on the FM-1: from flash).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sq80 {

struct RawWave {
    uint32_t start;     // byte offset into the wave ROM
    uint8_t sizeLog2;   // table length = 1 << sizeLog2 (256 .. 32768 bytes)
    uint8_t resolution; // DOC address resolution (0..7)
    int8_t tuneSemi;    // signed semitone offset of the sample
    uint8_t tuneFrac;   // 1/256 semitone
};

class RomPack {
public:
    static constexpr int kWaves = 75;
    static constexpr int kZones = 16;
    static constexpr uint32_t kWaveRomSize = 0x40000;

    // Returns false (and stays invalid) if `data` is not a version-1 ROM pack.
    bool load(const uint8_t* data, size_t size) {
        valid_ = false;
        if (size < 32 || std::memcmp(data, "SQ80PACK", 8) != 0) return false;
        const uint32_t version = rd32(data + 8), header = rd32(data + 12);
        const uint32_t waves = rd32(data + 16), raws = rd32(data + 20), romSize = rd32(data + 24);
        if (version != 1 || header != 32 || waves != kWaves || raws == 0 || raws > 255 ||
            romSize != kWaveRomSize)
            return false;
        size_t body = 6u * kWaves + 16u * kWaves + 4u * raws;
        body += (16 - body % 16) % 16;
        if (size < header + body + romSize) return false;
        names_ = data + header;
        zones_ = names_ + 6 * kWaves;
        rawTable_ = zones_ + 16 * kWaves;
        rawCount_ = static_cast<int>(raws);
        rom_ = data + header + body;
        for (int i = 0; i < kWaves * kZones; ++i)
            if (zones_[i] >= raws) return false;
        valid_ = true;
        return true;
    }

    bool valid() const { return valid_; }
    const uint8_t* rom() const { return rom_; }
    int rawCount() const { return rawCount_; }

    // Raw wave for a user wave (0..74) at a pitch zone (0..15).
    int rawIndex(int wave, int zone) const { return zones_[wave * kZones + zone]; }

    RawWave raw(int index) const {
        const uint8_t* r = rawTable_ + 4 * index;
        const uint8_t page = r[0], wsr = r[1];
        RawWave w;
        w.start = (static_cast<uint32_t>(wsr >> 6) << 16) | (static_cast<uint32_t>(page) << 8);
        w.sizeLog2 = static_cast<uint8_t>(8 + ((wsr >> 3) & 7));
        w.resolution = wsr & 7;
        w.tuneSemi = static_cast<int8_t>(r[2]);
        w.tuneFrac = r[3];
        return w;
    }

    // Wave name, 6 characters, space padded (e.g. "   SAW").
    void name(int wave, char out[7]) const {
        std::memcpy(out, names_ + 6 * wave, 6);
        out[6] = 0;
    }

private:
    static uint32_t rd32(const uint8_t* p) {
        return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
    const uint8_t* names_ = nullptr;
    const uint8_t* zones_ = nullptr;
    const uint8_t* rawTable_ = nullptr;
    const uint8_t* rom_ = nullptr;
    int rawCount_ = 0;
    bool valid_ = false;
};

}  // namespace sq80
