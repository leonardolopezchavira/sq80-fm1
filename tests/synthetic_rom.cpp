#include "synthetic_rom.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace sq80test {

std::vector<uint8_t> makeSyntheticRomPack() {
    const int waves = 75, raws = 3;
    std::vector<uint8_t> body;
    for (int w = 0; w < waves; ++w) {
        char name[7];
        std::snprintf(name, sizeof name, "WAVE%02d", w);
        body.insert(body.end(), name, name + 6);
    }
    for (int w = 0; w < waves; ++w)
        for (int z = 0; z < 16; ++z) body.push_back(w == 1 ? 1 : w == 60 ? 2 : 0);
    // raw table: [page, wsr, tuneSemi, tuneFrac]; wsr = bank<<6 | sizeCode<<3 | res
    const uint8_t raw[raws][4] = {{0x00, (2 << 3) | 1, 0, 0},   // 1024-byte sine at page 0
                                  {0x04, (1 << 3) | 1, 0, 0},   // 512-byte saw at page 4
                                  {0x10, (4 << 3) | 4, 0, 0}};  // 4096-byte one-shot at page 16
    for (const auto& r : raw) body.insert(body.end(), r, r + 4);
    while (body.size() % 16) body.push_back(0);

    std::vector<uint8_t> rom(0x40000, 0x80);
    const double pi = 3.14159265358979;
    for (int i = 0; i < 1024; ++i) rom[i] = static_cast<uint8_t>(128 + std::lround(126 * std::sin(2 * pi * i / 1024)));
    for (int i = 0; i < 512; ++i) rom[0x400 + i] = static_cast<uint8_t>(1 + i * 254 / 511);
    for (int i = 0; i < 4095; ++i) {
        const double env = std::exp(-i / 800.0);
        rom[0x1000 + i] = static_cast<uint8_t>(128 + std::lround(120 * env * std::sin(2 * pi * i / 64)));
        if (rom[0x1000 + i] == 0) rom[0x1000 + i] = 1;
    }
    rom[0x1000 + 4095] = 0;  // the terminator

    std::vector<uint8_t> pack(32, 0);
    std::memcpy(pack.data(), "SQ80PACK", 8);
    auto put = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) pack[at + i] = static_cast<uint8_t>(v >> (8 * i)); };
    put(8, 1); put(12, 32); put(16, waves); put(20, raws); put(24, 0x40000);
    pack.insert(pack.end(), body.begin(), body.end());
    pack.insert(pack.end(), rom.begin(), rom.end());
    return pack;
}

}  // namespace sq80test
