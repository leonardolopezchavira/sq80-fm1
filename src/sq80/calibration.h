// Numbers that set how the engine maps SQ-80 parameter values to sound. Where a value was
// measured, docs/measurements.md says how (black-box renders of the SQ8L 64-bit port, whose
// sound matches the original SQ8L sample for sample). The rest are first estimates, marked
// TODO(calibrate), to be replaced by measurements.
#pragma once

#include <cstdint>

namespace sq80::cal {

// The SQ-80's DOC 5503 sample clock and the OS control (envelope/LFO) rate.
constexpr float kDocRate = 38455.855f;
constexpr float kControlRate = 83.592575f;

// Envelope segment length in control ticks for TIME 0..63 (measured, docs/measurements.md).
constexpr uint16_t kEnvTicks[64] = {
    2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 5, 6, 7, 7, 8, 9, 10, 11, 12, 14, 15, 17, 19, 21, 24, 26,
    30, 33, 37, 41, 46, 52, 58, 65, 73, 82, 92, 103, 115, 129, 145, 161, 181, 203, 228, 257,
    288, 323, 363, 405, 457, 513, 574, 644, 725, 809, 913, 1025, 1151, 1292, 1450, 1627,
    1827, 2049};

// Second half of a "R" (second release) envelope: fixed time 39.
constexpr int kSecondReleaseTime = 39;

// Pitch: one table traversal of a single-cycle wave at key 69 = 440 Hz.
constexpr float kA4 = 440.0f;
constexpr int kA4Key = 69;

// Modulation sum: (source * depth) / 64, source in SQ-80 units (about +-127), depth +-63.
// Destination scales below convert that sum.
constexpr float kPitchModSemitonesPerUnit = 12.0f / 124.0f;  // TODO(calibrate)
constexpr float kLevelModPerUnit = 0.5f;                      // DCA level steps, TODO(calibrate)
constexpr float kCutoffModPerUnit = 0.5f;                     // FILTER FREQ steps (SQ8L: FREQ*2 + sum)
constexpr float kPanModPerUnit = 1.0f / 16.0f;                // PAN steps, TODO(calibrate)

// Filter: FREQ (0..127) to Hz, TODO(calibrate).
constexpr float kCutoffHzAt0 = 30.0f;
constexpr float kCutoffSemitonesPerStep = 1.0f;
// KEYBD 0..63: cutoff tracking in FREQ steps per key at KEYBD 63, TODO(calibrate).
constexpr float kKeyTrackAtMax = 1.0f;
constexpr int kKeyTrackCenter = 60;
// RES 0..31 to ladder feedback (4 = self-oscillation), TODO(calibrate).
constexpr float kResonanceAtMax = 3.9f;

// DCA: amplitude for a level 0..63, as dB below full. TODO(calibrate) (the DOC volume is
// linear; the SQ-80 OS maps levels through a curve).
constexpr float kDcaDbPerStep = 0.75f;

// Output level: one oscillator at full level matches the reference renders.
constexpr float kOutputGain = 1.2f;  // measured: matches the reference level (docs/measurements.md)

// Glide: GLIDE 0..127 to seconds per octave, TODO(calibrate).
constexpr float kGlideSecondsPerOctaveAt127 = 4.0f;

}  // namespace sq80::cal
