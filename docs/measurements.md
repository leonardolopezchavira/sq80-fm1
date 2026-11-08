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

## Still estimates

Filter cutoff curve, key tracking and resonance; pitch, level, filter and pan modulation depths;
DCA level curve; glide time. Marked `TODO(calibrate)` in `src/sq80/calibration.h`.
