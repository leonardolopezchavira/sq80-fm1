/* SPDX-License-Identifier: MIT */
#ifndef SQ80_CAL_H
#define SQ80_CAL_H
#include <stdint.h>

// Numbers that set how the engine maps SQ-80 parameter values to sound. Where a value was
// measured, docs/measurements.md says how (black-box renders of the SQ8L 64-bit port, whose
// sound matches the original SQ8L sample for sample).

// The SQ-80's DOC 5503 sample clock and the OS control (envelope/LFO) rate.
static const float SQ80_DOC_RATE = 38455.855f;
static const float SQ80_CONTROL_RATE = 83.592575f;

// Envelope segment length in control ticks for TIME 0..63 (measured, docs/measurements.md).
static const uint16_t SQ80_ENV_TICKS[64] = {
    2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 5, 6, 7, 7, 8, 9, 10, 11, 12, 14, 15, 17, 19, 21, 24, 26,
    30, 33, 37, 41, 46, 52, 58, 65, 73, 82, 92, 103, 115, 129, 145, 161, 181, 203, 228, 257,
    288, 323, 363, 405, 457, 513, 574, 644, 725, 809, 913, 1025, 1151, 1292, 1450, 1627,
    1827, 2049};

// Second half of a "R" (second release) envelope: fixed time 39.
#define SQ80_SECOND_RELEASE_TIME 39

// Pitch: one table traversal of a single-cycle wave at key 69 = 440 Hz.
static const float SQ80_A4 = 440.0f;
#define SQ80_A4_KEY 69

// Modulation sum: (source * depth) / 64, source in SQ-80 units (about +-127), depth +-63.
// Destination scales below convert that sum.
static const float SQ80_PITCH_MOD_SEMITONES_PER_UNIT = 0.2459f;  // measured: depth d with ENV at +63 = 0.484 d semitones
static const float SQ80_LEVEL_MOD_PER_UNIT = 0.48f;  // DCA level steps; measured: depth d at ENV +63 = about 0.95 d steps
static const float SQ80_CUTOFF_MOD_PER_UNIT = 0.5f;  // FILTER FREQ steps; measured: depth d at ENV +63 = d steps
static const float SQ80_PAN_MOD_PER_UNIT = 1.0f / 16.0f;  // PAN steps; measured: depth 32 at ENV +63 = 4 steps

// Filter cutoff parameter in Hz for FREQ 0..127 (measured, docs/measurements.md): the
// reference's -6 dB points, divided by 0.66 (where this 4-pole ladder's -6 dB point falls),
// extended with the measured slopes below FREQ 20 and above 96, then corrected above FREQ 76
// for this digital filter's bend near the top of the audio range (measured again).
static const float SQ80_CUTOFF_HZ[128] = {
    52.6f, 55.9f, 59.4f, 63.2f, 67.2f, 71.5f, 76.0f, 80.8f,
    85.9f, 91.3f, 97.1f, 103.3f, 109.8f, 116.8f, 124.2f, 132.0f,
    140.4f, 149.3f, 158.7f, 168.8f, 179.4f, 190.6f, 202.4f, 215.0f,
    228.4f, 239.7f, 251.7f, 264.2f, 277.3f, 299.1f, 322.6f, 347.9f,
    375.2f, 397.6f, 421.3f, 446.4f, 473.1f, 499.4f, 527.1f, 556.4f,
    587.3f, 631.1f, 678.1f, 728.7f, 783.0f, 827.9f, 875.4f, 925.7f,
    978.8f, 1017.2f, 1057.2f, 1098.7f, 1141.9f, 1248.4f, 1364.8f, 1492.1f,
    1631.3f, 1700.2f, 1772.0f, 1846.9f, 1924.9f, 2070.7f, 2227.4f, 2396.1f,
    2577.5f, 2774.0f, 2985.6f, 3213.3f, 3458.4f, 3663.7f, 3881.1f, 4111.5f,
    4355.6f, 4735.3f, 5148.1f, 5596.9f, 5988.5f, 6418.1f, 6878.7f, 7401.4f,
    7963.8f, 8523.0f, 9121.4f, 9761.9f, 10447.4f, 11166.0f, 11933.9f, 12754.6f,
    13631.9f, 14372.5f, 15153.4f, 15976.7f, 16844.7f, 17680.8f, 18558.3f, 19479.2f,
    20446.0f, 21896.9f, 23450.7f, 25114.6f, 26896.8f, 28805.3f, 30849.4f, 33038.3f,
    35382.6f, 37893.3f, 40582.1f, 43461.7f, 46545.7f, 49848.4f, 53385.6f, 57173.6f,
    61230.6f, 65575.4f, 70228.5f, 75211.8f, 80548.7f, 86264.2f, 92385.4f, 98940.7f,
    105961.5f, 113480.2f, 121532.5f, 130156.1f, 139391.8f, 149282.7f, 159875.5f, 171219.9f};

// KEYBD 0..63: the cutoff rises by KEYBD/63 FREQ steps per key above key 0 (measured: KEYBD 63
// adds about 60 steps at key 60, 33 at key 36).
static const float SQ80_KEY_TRACK_AT_MAX = 1.0f;
#define SQ80_KEY_TRACK_CENTER 0

// RES 0..31 to ladder feedback (4 = self-oscillation), fitted so the resonance peak above the
// passband matches the reference (5.2 / 11.1 / 18.6 / 28.0 dB at RES 8 / 16 / 24 / 31).
static const float SQ80_RESONANCE[32] = {
    0.000f, 0.195f, 0.389f, 0.584f, 0.778f, 0.973f, 1.168f, 1.362f,
    1.557f, 1.673f, 1.789f, 1.906f, 2.022f, 2.138f, 2.255f, 2.371f,
    2.487f, 2.584f, 2.681f, 2.778f, 2.875f, 2.972f, 3.069f, 3.166f,
    3.263f, 3.326f, 3.389f, 3.451f, 3.514f, 3.577f, 3.640f, 3.700f};

// Filter input drive: tanh(drive * x) / drive. Low: the filter stays nearly linear, so the
// resonance fit above holds for loud input too.
static const float SQ80_FILTER_DRIVE = 0.1f;
// Passband compensation: the input enters as (1 + c*k) x, so resonance costs less bass, as on
// the reference (measured bass loss -5.7 / -7.6 / -9.1 / -10.1 dB at RES 8 / 16 / 24 / 31).
static const float SQ80_FILTER_BASS_COMPENSATION = 0.15f;

// DCA1-3: level (0..63) to dB relative to level 63 (measured at these levels, linear between).
#define SQ80_DCA_POINTS 10
static const float SQ80_DCA_LEVEL[SQ80_DCA_POINTS] = {0.0f, 1.0f, 8.0f, 16.0f, 24.0f, 32.0f, 40.0f, 48.0f, 56.0f, 63.0f};
static const float SQ80_DCA_DB[SQ80_DCA_POINTS] = {-96.0f, -59.6f, -45.1f, -43.3f, -35.3f, -25.8f, -17.7f, -10.4f, -4.0f, 0.0f};

// DCA4 and ENV4: gain = ((ENV4 level / 63) x (FINAL / 63)) ^ 2.5 (measured: FINAL 48 / 32 / 16 =
// -5.8 / -14.6 / -29.9 dB; ENV4 velocity scaling follows the same curve).
static const float SQ80_FINAL_EXPONENT = 2.5f;

// Output level: one oscillator at full level matches the reference renders.
static const float SQ80_OUTPUT_GAIN = 1.02f;  // measured: matches the reference level (docs/measurements.md)

// Glide: the whole glide takes kEnvTicks[min(GLIDE, 63)] control ticks, whatever the interval
// (the SQ8L port's documentation: the envelope time table also times the glide).

#endif
