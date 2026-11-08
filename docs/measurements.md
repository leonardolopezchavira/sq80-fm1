# Measurements

Black-box renders of the SQ8L 64-bit port (`tools/oracle/`), which reproduces the original
SQ8L sample for sample. Only its audio output is measured.

## Envelope time table (`kEnvTicks`)

`tools/oracle/measure_env_times.py`: ENV4 falls from L1 = 63 to 0 in T2 = t (T1 = 0) on a held
SINE; the segment ends when the level stops falling. Values are control ticks at 83.592575 Hz.

- 2 ticks (24 ms) at TIME 0..2, 2049 ticks (24.5 s) at 63.
- Above the bottom of the range the time doubles every 6 steps: TIME = 6k + 3 lasts exactly
  2^(k+1) + 1 ticks (checked by timing the segment to true silence).

## Wave pitch (`setupOscillator`)

Every wave 0..74 played with the filter open at middle C (and PIANO at keys 36, 45):

- pitch = 440 Hz x 2^((note + octave*12 + semitone + fine/32 + tune_semi + tune_frac/256 - 69)/12)
  x 2^(1 - resolution), with tune and resolution from the raw-wave table of the OS ROM.
  The resolution term matters for the long samples (resolution 3-4), which otherwise play
  2-3 octaves high.
- Pitch zone = (note + octave*12 + semitone) / 8.
- Result: same pitch for all tonal waves, spectra within 0.3-4.6 dB, envelope correlation
  0.97-1.0.

## Output level (`kOutputGain`)

One oscillator at DCA 63, DCA4 63, filter open: 1.2 makes the level match the reference
(within 0.4 dB on SAW, SINE and BOWING).

## Filter (`kCutoffHz`, `kResonance`, key tracking, drive, bass compensation)

`tools/oracle/measure_filter.py`: NOISE1 through the filter, spectrum relative to the open
filter, at FREQ 0..124 (step 4), RES 0/8/16/24/31, KEYBD 0/32/63 at keys 12..60, and ENV3 at
+63 into the cutoff with depths -16..63.

- Cutoff (-6 dB point): about one semitone per FREQ step in the middle (248 Hz at 32, 1.08 kHz
  at 56, 5.5 kHz at 80), flatter below FREQ 20, steeper above 80; open from about FREQ 100.
  `kCutoffHz` reproduces it within 5 % from FREQ 20 to 88.
- Envelope depth: depth d with the envelope at +63 moves the cutoff d FREQ steps.
- Key tracking: KEYBD/63 FREQ steps per key, counted from key 0 (+60 steps at key 60 with
  KEYBD 63, +30 with KEYBD 32).
- Resonance: bass loss -5.7 / -7.6 / -9.1 / -10.1 dB and peak -0.5 / 3.5 / 9.5 / 17.9 dB (relative
  to the open filter) at RES 8 / 16 / 24 / 31. This engine: within 0.7 dB of the bass loss and
  0.6 dB of the peaks up to RES 24 (+1.8 dB at 31), with a nearly linear input (drive 0.1) and
  passband compensation (1 + 0.15 k).

## Modulation and levels (`tools/oracle/measure_mods.py`)

- Pitch: depth d with the source at +63 (an envelope) = 0.484 d semitones.
- LFO: same rate and swing as the reference (1.13 Hz at FREQ 10, 4.51 Hz at FREQ 20).
- DCA1-3 level curve (dB below level 63): 56 -4.0, 48 -10.4, 40 -17.7, 32 -25.8, 24 -35.3,
  16 -43.3, 8 -45.1, 1 -59.6 (`kDcaLevel`/`kDcaDb`); level modulation about 0.95 steps per depth
  step at +63.
- DCA4 and ENV4: gain = ((ENV4/63) x (FINAL/63))^2.5 (FINAL 48 / 32 / 16 = -5.8 / -14.6 / -29.9
  dB); velocity (LV 63) follows: -7 / -15 / -30 dB at velocity 96 / 64 / 32.
- Pan: linear law, PAN 8 = centre, 0 / 15 = hard left / right, 4 / 12 = -/+9.7 dB; depth 32 at
  +63 = 4 PAN steps.
- All of these now match within about 0.5 dB / 0.5 semitone (velocity below 16 is quieter than
  the reference's floor).

## Whole patches

`tools/oracle/compare.py` at middle C (factory bank) and at keys 48/72 (random programs):

| Set | Spectrum difference (median) | Envelope correlation | Level | Within 6 dB |
|---|---|---|---|---|
| 40-program factory bank, before calibration | 21.2 dB | 0.91 | -2.4 dB | |
| 40-program factory bank, now | 2.7 dB | 0.99 | +0.2 dB | 38/40 |
| 120 random programs from 5,000+, keys 48/72 | 2.8 dB (90th percentile 6.7) | 0.99 | +0.1 dB | 104/120 |

The largest differences in the random set are programs that are (almost) silent at that key in
both engines, where a spectrum comparison means nothing.

## Not measured yet

Glide (timed with the envelope table, as documented for SQ8L), the second release ("R"), mono
and voice-restart behaviour, AM and SYNC on whole patches, humanize.
