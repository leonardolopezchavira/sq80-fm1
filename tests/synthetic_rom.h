// A ROM pack with synthetic waves (no Ensoniq data), for tests and CI.
#pragma once
#include <cstdint>
#include <vector>

namespace sq80test {
// Every user wave maps to raw wave 0 (a 1024-byte sine) in every zone, except wave 1 -> raw 1
// (a 512-byte saw) and wave 60 -> raw 2 (a 4096-byte decaying one-shot ending in a zero byte).
std::vector<uint8_t> makeSyntheticRomPack();
}
