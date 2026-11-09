# SQ-80 for the M-VAVE FM-1

An unofficial Ensoniq SQ-80 / ESQ-1 engine, written from scratch to run as custom firmware on
the **M-VAVE FM-1** pocket synthesizer. It plays ESQ-1/SQ-80 SysEx patches with the SQ-80's own
waveforms, read from **your** ROM dumps.

> **Unofficial.** Not affiliated with M-VAVE, Ensoniq or the SQ8L authors. **No Ensoniq ROM
> data is in this repository**; you build a ROM pack from your own dumps (see below).

## Status

| Step | State |
|---|---|
| 1. Engine on the desktop, compared against SQ8L | **done** (calibrated, see below) |
| 2. Move to the FM-1 (fm1-base on felucca-bsp), front-panel UI | in progress: SQ-80 0.3 (ROM pack and banks from flash) |
| 3. Test on hardware | not started |

What works now (desktop):

- **ROM pack** built from your SQ-80 OS ROM and wave ROMs: 75 waves, 151 raw samples, 16 pitch
  zones each (`tools/make_rompack.py`, 264 KB, it fits the FM-1's flash data area).
- **Patches**: ESQ-1/SQ-80 single-program and 40-program bank SysEx.
- **Oscillators**: DOC 5503-style, running at the DOC's 38,455.855 Hz and resampled, with the
  SQ-80's pitch zones, one-shots, SYNC and AM. **All 75 waves match the reference in pitch,
  envelope and spectrum.**
- **Envelopes** with the measured SQ-80 time table, LV/T1V/TK, second release, full cycle.
- **LFOs** (TRI/SAW/SQR/NOISE, delay fade, wheel depth), 8-voice allocation, mono and glide.
- 4-pole resonant filter, DCAs, DCA4 and pan, calibrated by measurement: cutoff curve, key
  tracking, resonance, modulation depths and level curves.

How close whole patches are (`tools/oracle/compare.py`): on a 40-program factory bank the
median spectrum difference is **2.7 dB**, envelopes correlate at **0.99** and levels are within
0.2 dB; on 120 random programs from a 5,000-program collection, at two other keys, **2.8 dB** and
0.99, with 104 of 120 within 6 dB. Details: [docs/measurements.md](docs/measurements.md).

## Build (desktop)

```sh
cmake -S . -B build && cmake --build build
./build/sq80_tests                      # no ROMs needed: uses synthetic waves
```

## Your ROM pack

```sh
python3 tools/make_rompack.py --os-low sq80rom.low --os-high sq80rom.hig \
    --wave 2202.bin 2204.bin 2203.bin 2205.bin -o sq80.rompack
# or: --wave "SQ80m voice rom 020.bin"
./build/sq80_render sq80.rompack bank.syx 0 out.wav 60 100 2.0   # program 0, middle C
```

## How it is checked

`tools/oracle/` renders the same notes through the SQ8L 64-bit port
([sq8l-revival/SQ8L](https://github.com/sq8l-revival/SQ8L)), whose output matches the original
SQ8L sample for sample, and measures the difference. The port's engine is **not** part of this
repository and none of its code is used: it is only played and listened to, like a hardware
SQ-80 would be. What was measured is in [docs/measurements.md](docs/measurements.md).

## The FM-1 firmware

`app/` is an [fm1-base](https://github.com/charlesvestal/fm1-base) app (fm1-base is the `fm1/base`
submodule): **SQ-80**, the engine on the FM-1, eight voices over its two cores. It plays your ROM pack and
up to eight ESQ-1/SQ-80 program banks, loaded into its flash over USB SysEx by the loader page
(`app/loader/index.html`, also at https://leonardolopezchavira.github.io/sq80-fm1/load/; the protocol is in
`app/app.c` and `tools/fm1_loader.py`). Without them it plays a synthetic test wave set.

- Install: https://leonardolopezchavira.github.io/sq80-fm1/install/ (Chrome or Edge).
- Simulator: `git submodule update --init --recursive`, `python3 fm1/base/tools/build.py app --host`,
  then `app/build/host/sq80_host app/tests/basic.txt app/build/host`; the loader:
  `python3 tools/fm1_loader.py sim-script /tmp/l.txt && app/build/host/sq80_host /tmp/l.txt app/build/host`.
- Firmware: the `firmware` workflow builds release `app/VERSION` with JieLi's toolchain (the shared
  [fm1-build](https://github.com/zvenson/fm1-build) workflow) and publishes the site.

Measured on the FM-1 (0.2.0): eight voices at 39% audio load on two cores, no under-runs.

## Effects

The firmware has FMini-1's effects (https://github.com/ruppert/fmini-1, after Felucca by Leo Kuroshita, Hügelton
Instruments), ported as they are: DRIVE (SOFT, TUBE), CHORUS (STEREO, JUNO, MULTI), DELAY (ANALOG, DIGITAL, TAPE,
P-PONG, MULTI, SLAP, REVERSE) and REVERB (ROOM, HALL, PLATE, SPRING, CLOUD), on FX. They are GPL-3.0-only, so the
firmware as a whole is too: see [LICENSING.md](LICENSING.md).

## Layout

- `src/sq80/`: the engine (portable C99, single-precision float, no OS calls, no allocation), the same source for the desktop tools and the FM-1
- `app/`: the FM-1 app (fm1-base), its loader page in `app/loader/`; `fm1/base`: fm1-base
- `tools/`: ROM pack builder, desktop renderer, patch codec, oracle comparisons
- `tests/`: unit tests with a synthetic ROM pack
- `docs/`: formats and measurements

## Credits

The SysEx byte map and the DOC/envelope/LFO behaviour follow the MIT-licensed documentation of
the SQ8L 64-bit port (`docs/modules/*.md` there). The SQ-80 wave directory was decoded from the
OS ROM in this project. SQ8L is by Siegfried Kullmann; Ensoniq, ESQ-1 and SQ-80 are trademarks
of their owners.
