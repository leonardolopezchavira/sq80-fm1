// Desktop renderer: plays notes through the engine and writes a 16-bit stereo WAV.
//
//   sq80_render <rompack> <bank.syx> <program#> <out.wav> [key vel seconds] [--hold s] [--sr hz]
//
// Program numbers start at 0 (the first program of the file). Used to compare the engine
// with reference renders (tools/oracle) and to listen to it before it goes on the FM-1.
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "sq80/patch.h"
#include "sq80/rompack.h"
#include "sq80/synth.h"

static std::vector<uint8_t> readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

static void writeWav(const char* path, const std::vector<float>& l, const std::vector<float>& r, int sr) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) { std::perror(path); std::exit(1); }
    const uint32_t frames = static_cast<uint32_t>(l.size()), data = frames * 4;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(sr); u32(sr * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(data);
    for (uint32_t i = 0; i < frames; ++i) {
        for (float x : {l[i], r[i]}) {
            const float c = x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x;
            u16(static_cast<uint16_t>(static_cast<int16_t>(c * 32767.0f)));
        }
    }
    std::fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <rompack> <bank.syx> <program#> <out.wav> [key vel seconds] [--sr hz] [--raw out.f32]\n", argv[0]);
        return 2;
    }
    std::vector<uint8_t> pack = readFile(argv[1]);
    sq80::RomPack rom;
    if (!rom.load(pack.data(), pack.size())) { std::fprintf(stderr, "%s: not a ROM pack\n", argv[1]); return 1; }

    std::vector<uint8_t> syx = readFile(argv[2]);
    const int want = std::atoi(argv[3]);
    sq80::SysexReader reader(syx.data(), syx.size());
    uint8_t prog[102];
    int index = 0;
    bool found = false;
    while (reader.next(prog))
        if (index++ == want) { found = true; break; }
    if (!found) { std::fprintf(stderr, "program %d not found (%d in file)\n", want, index); return 1; }

    int key = 69, vel = 100;
    float seconds = 2.0f, sr = 44100.0f;
    const char* raw = nullptr;
    int pos = 0;
    for (int i = 5; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--sr") && i + 1 < argc) sr = std::strtof(argv[++i], nullptr);
        else if (!std::strcmp(argv[i], "--raw") && i + 1 < argc) raw = argv[++i];
        else if (pos == 0) { key = std::atoi(argv[i]); ++pos; }
        else if (pos == 1) { vel = std::atoi(argv[i]); ++pos; }
        else if (pos == 2) { seconds = std::strtof(argv[i], nullptr); ++pos; }
    }

    sq80::Patch patch;
    sq80::decodePatch(prog, patch);
    sq80::Synth synth(&rom, sr);
    synth.setPatch(patch);
    const int total = static_cast<int>(sr * (seconds + 1.5f));
    const int noteOffAt = static_cast<int>(sr * seconds);
    std::vector<float> l(total), r(total);
    synth.noteOn(key, vel);
    for (int pos2 = 0; pos2 < total; pos2 += 64) {
        if (pos2 <= noteOffAt && noteOffAt < pos2 + 64) synth.noteOff(key);
        const int n = std::min(64, total - pos2);
        synth.render(&l[pos2], &r[pos2], n);
    }
    writeWav(argv[4], l, r, static_cast<int>(sr));
    if (raw) {
        std::FILE* f = std::fopen(raw, "wb");
        std::fwrite(l.data(), 4, l.size(), f);
        std::fwrite(r.data(), 4, r.size(), f);
        std::fclose(f);
    }
    std::printf("%s: \"%s\" key %d vel %d, %.1f s\n", argv[4], patch.name, key, vel, seconds);
    return 0;
}
