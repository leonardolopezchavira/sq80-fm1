#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""SQ-80's built-in waves: an original set of 75, made here from scratch, in the ROM pack format (docs/rompack.md).

Nothing of Ensoniq's is read or used: each wave is synthesized from a recipe written for its kind of sound (additive
spectra, formant envelopes, waveshapes, noise, plucked-string and drum models). The names and the slots follow the
SQ-80's own list, so a program that asks for wave 9 gets this set's PIANO; the one-shot waves are 54..74, as the
engine expects. A ROM pack made from the player's own dumps (tools/make_rompack.py) replaces the whole set.

    gen_waves.py OUT.rompack [--header app/waves/sq80_waves.h]

Layout: every looped wave has a table for each of four key ranges (zones 0-5, 6-8, 9-10, 11-15: keys 0-47, 48-71,
72-87, 88-127), with no more harmonics than the DOC (38.46 kHz) can play there, and formant waves evaluate their
formants at each range's own pitch; inharmonic waves (bells, metal) loop four cycles of their fundamental; noise
waves loop 4096 samples; one-shots play once at 24 kHz at middle C, and DRUMS1-5 are kits across the keyboard.
"""
import argparse
import struct
import sys
from pathlib import Path

import numpy as np

DOC = 38455.855
NAMES = ["   SAW", "  BELL", "  SINE", "SQUARE", " PULSE", "NOISE1", "NOISE2", "NOISE3", "  BASS", " PIANO",
         "EL PNO", "VOICE1", "VOICE2", "  KICK", "  REED", " ORGAN", "SYNTH1", "SYNTH2", "SYNTH3", "FORMT1",
         "FORMT2", "FORMT3", "FORMT4", "FORMT5", "PULSE2", "SQR  2", "4 OCTS", " PRIME", "BASS 2", "E PNO2",
         "OCTAVE", " OCT+5", "SAW  2", "TRIANG", "REED 2", "REED 3", "GRIT 1", "GRIT 2", "GRIT 3", "GLINT1",
         "GLINT2", "GLINT3", "  CLAV", " BRASS", "STRING", "DIGIT1", "DIGIT2", "BELL 2", " ALIEN", "BREATH",
         "VOICE3", " STEAM", " METAL", " CHIME", "BOWING", "PICK 1", "PICK 2", "MALLET", "  SLAP", " PLINK",
         " PLUCK", " PLUNK", " CLICK", " CHIFF", " THUMP", "LOGDRM", "KICK 2", " SNARE", "TOMTOM", "HI-HAT",
         "DRUMS1", "DRUMS2", "DRUMS3", "DRUMS4", "DRUMS5"]
assert len(NAMES) == 75 and all(len(n) == 6 for n in NAMES)

# the four key ranges of a looped wave: zones (of 8 keys), most harmonics, the key its formants are heard at
RANGES = [(range(0, 6), 127, 36), (range(6, 9), 54, 60), (range(9, 11), 18, 79), (range(11, 16), 8, 94)]
RS = 24000.0              # one-shots: their sample rate at middle C
C4 = 261.6256


def hz(key):
    return 440.0 * 2 ** ((key - 69) / 12)


rng = np.random.default_rng(1980)   # (fixed: the same set every build)


# ------------------------------------------------------------------------------------------------ tables
def from_harmonics(amps, phases=None, n=256):
    """one cycle of n samples from harmonic amplitudes amps[0] = the fundamental (sine phase unless phases)"""
    t = np.arange(n) / n
    x = np.zeros(n)
    for k, a in enumerate(amps, start=1):
        if a and k < n // 2:
            x += a * np.sin(2 * np.pi * k * t + (phases[k - 1] if phases is not None else 0.0))
    return x


def harmonics_of(shape, hmax):
    """the harmonics (amplitudes, phases) of one cycle given as a function of t in [0, 1), up to hmax"""
    n = 8192
    s = np.fft.rfft(shape(np.arange(n) / n))
    k = np.arange(1, hmax + 1)
    a = np.abs(s[k]) * 2 / n
    p = np.angle(s[k]) + np.pi / 2     # (cos -> sin phase)
    return a, p


def formant_env(freqs, formants, tilt=0.0, floor=0.08):
    """a spectral envelope: resonances (centre Hz, bandwidth Hz, gain), a tilt in dB per octave above 200 Hz"""
    f = np.asarray(freqs, float)
    e = np.full_like(f, floor)
    for fc, bw, g in formants:
        e += g / (1 + ((f - fc) / (bw / 2)) ** 2)
    if tilt:
        e *= 10 ** (tilt * np.log2(np.maximum(f, 200) / 200) / 20)
    return e


def to_bytes(x, peak=120.0, rms=None):
    """8-bit, 1..255 (0 would stop the DOC), its peak at peak, or quieter where rms caps it"""
    x = np.asarray(x, float)
    m = np.max(np.abs(x))
    if m > 0:
        g = peak / m
        if rms:
            g = min(g, rms / np.sqrt(np.mean(x ** 2)))
        x = x * g
    return np.clip(np.round(128 + x), 1, 255).astype(np.uint8)


# ------------------------------------------------------------------------------------------------ recipes
# A looped harmonic wave: a function (h, f0) -> (amps, phases or None) for h harmonics at fundamental f0 (the
# range's pitch, for formants)
def saw(h, f0): return 1 / np.arange(1, h + 1), None


def odd(a, h):
    k = np.arange(1, h + 1)
    return np.where(k % 2 == 1, a, 0.0)


def pulse(d):
    def f(h, f0):
        k = np.arange(1, h + 1)
        return np.abs(np.sin(np.pi * k * d)) / k, np.where(np.sin(np.pi * k * d) >= 0, np.pi / 2, -np.pi / 2)
    return f


def shaped(shape):
    def f(h, f0):
        return harmonics_of(shape, h)
    return f


def through(source, formants, tilt=0.0, floor=0.08):
    """a source spectrum through formants evaluated at each harmonic's frequency"""
    def f(h, f0):
        a, p = source(h, f0)
        k = np.arange(1, h + 1)
        return a * formant_env(k * f0, formants, tilt, floor), p
    return f


def jitter(source, amount, seed):
    def f(h, f0):
        a, p = source(h, f0)
        r = np.random.default_rng(seed).uniform(1 - amount, 1 + amount, h)
        return a * r, p
    return f


def phases_random(source, seed):
    def f(h, f0):
        a, _ = source(h, f0)
        return a, np.random.default_rng(seed).uniform(0, 2 * np.pi, h)
    return f


def power(e):
    return lambda h, f0: (1 / np.arange(1, h + 1) ** e, None)


def custom(d):
    """harmonic number -> amplitude"""
    def f(h, f0):
        return np.array([d.get(k, 0.0) for k in range(1, h + 1)]), None
    return f


def mix(*parts):
    def f(h, f0):
        tot = np.zeros(h)
        for w, src in parts:
            a, _ = src(h, f0)
            tot += w * a
        return tot, None
    return f


def post(source, fn):
    """the cycle made, then shaped in time (fn: samples -> samples); keeps its harmonics bounded by h afterwards"""
    def f(h, f0):
        a, p = source(127, f0)
        x = from_harmonics(a, p, 4096)
        y = fn(x / np.max(np.abs(x)))
        return harmonics_of(lambda t: np.interp(t * 4096, np.arange(4097), np.append(y, y[0])), h)
    return f


LOOPED = {
    "SAW": saw,
    "SINE": custom({1: 1.0}),
    "SQUARE": lambda h, f0: (odd(1 / np.arange(1, h + 1), h), None),
    "PULSE": pulse(0.25),
    "PULSE2": pulse(0.125),
    "SQR 2": lambda h, f0: (odd(1 / np.arange(1, h + 1) ** 1.35, h) + np.where(np.arange(1, h + 1) % 2 == 0,
                                                                            0.08 / np.arange(1, h + 1), 0), None),
    "SAW 2": lambda h, f0: (np.where(np.arange(1, h + 1) % 2, 1.3, 0.8) / np.arange(1, h + 1) ** 0.95, None),
    "TRIANG": lambda h, f0: (odd(1 / np.arange(1, h + 1) ** 2, h), np.where((np.arange(1, h + 1) // 2) % 2, np.pi, 0)),
    "OCTAVE": lambda h, f0: (1 / np.arange(1, h + 1) + np.where(np.arange(1, h + 1) % 2 == 0, 1.6 / np.arange(1, h + 1), 0), None),
    "OCT+5": custom({1: 1.0, 2: 0.8, 3: 0.7, 4: 0.25, 6: 0.18, 8: 0.08}),
    "4 OCTS": custom({1: 1.0, 2: 0.8, 4: 0.65, 8: 0.5, 16: 0.25}),
    "PRIME": custom({1: 1.0, 2: 0.7, 3: 0.55, 5: 0.45, 7: 0.35, 11: 0.25, 13: 0.2, 17: 0.12, 19: 0.1, 23: 0.07}),
    "ORGAN": custom({1: 0.9, 2: 0.9, 3: 0.8, 4: 0.55, 6: 0.3, 8: 0.35, 10: 0.12, 12: 0.15, 16: 0.2}),
    "BASS": through(power(1.5), [(110, 160, 1.4), (700, 500, 0.5)], tilt=-4),
    "BASS 2": through(saw, [(180, 200, 1.0), (900, 600, 0.9), (2400, 1200, 0.3)], tilt=-2),
    "PIANO": jitter(through(lambda h, f0: (np.abs(np.sin(np.pi * np.arange(1, h + 1) / 7.3)) / np.arange(1, h + 1) ** 0.95,
                                           None), [(500, 900, 1.0), (2600, 2200, 0.45)], tilt=-3, floor=0.15), 0.12, 9),
    "EL PNO": custom({1: 1.0, 2: 0.32, 3: 0.12, 4: 0.05, 7: 0.09, 8: 0.11, 9: 0.06}),
    "E PNO2": custom({1: 1.0, 2: 0.55, 3: 0.42, 4: 0.12, 5: 0.2, 6: 0.05, 7: 0.08, 9: 0.04}),
    "VOICE1": through(power(1.0), [(710, 130, 1.0), (1150, 140, 0.55), (2500, 200, 0.28), (3400, 300, 0.12)], tilt=-2),
    "VOICE2": through(power(1.0), [(320, 90, 1.0), (850, 120, 0.4), (2300, 200, 0.12)], tilt=-3),
    "VOICE3": through(power(1.0), [(280, 80, 1.0), (2250, 180, 0.45), (2950, 240, 0.35), (3600, 300, 0.15)], tilt=-2),
    "KICK": custom({1: 1.0, 2: 0.35, 3: 0.12, 4: 0.05}),
    "REED": through(lambda h, f0: (odd(1 / np.arange(1, h + 1) ** 0.85, h) + 0.06 / np.arange(1, h + 1), None),
                    [(1500, 900, 0.8)], tilt=-2, floor=0.4),
    "REED 2": through(power(0.6), [(1100, 500, 1.0), (2800, 900, 0.35)], tilt=-3, floor=0.15),
    "REED 3": through(power(0.75), [(650, 400, 0.6), (1800, 900, 1.0), (3300, 1000, 0.3)], tilt=-1, floor=0.2),
    "SYNTH1": lambda h, f0: ((1 / np.arange(1, h + 1)) * (1 + 2.2 * np.exp(-((np.arange(1, h + 1) - 8) / 2.2) ** 2))
                             + odd(0.4 / np.arange(1, h + 1), h), None),
    "SYNTH2": lambda h, f0: ((1 / np.arange(1, h + 1)) * (1 + 4 * np.exp(-((np.arange(1, h + 1) - 5) / 1.6) ** 2)), None),
    "SYNTH3": mix((1.0, pulse(0.3)), (0.8, pulse(0.45))),
    "FORMT1": through(saw, [(450, 180, 1.0)], floor=0.15),
    "FORMT2": through(saw, [(800, 200, 1.0)], floor=0.12),
    "FORMT3": through(saw, [(1300, 260, 1.0)], floor=0.12),
    "FORMT4": through(saw, [(2100, 320, 1.0)], floor=0.1),
    "FORMT5": through(saw, [(3200, 420, 1.0)], floor=0.1),
    "GRIT 1": phases_random(jitter(power(0.55), 0.7, 21), 11),
    "GRIT 2": phases_random(jitter(power(0.35), 0.9, 22), 12),
    "GRIT 3": post(saw, lambda x: np.round(x * 3.5) / 3.5),
    "CLAV": through(lambda h, f0: (np.abs(np.sin(np.pi * np.arange(1, h + 1) * 0.09)) / np.arange(1, h + 1) ** 0.6, None),
                    [(1200, 1400, 1.0)], tilt=0, floor=0.5),
    "BRASS": through(power(0.7), [(1200, 900, 1.0), (2600, 900, 0.35)], tilt=-5, floor=0.25),
    "STRING": jitter(through(saw, [(450, 300, 0.7), (1100, 500, 0.6), (2600, 1200, 0.45)], tilt=-2, floor=0.35), 0.25, 31),
    "DIGIT1": post(mix((1.0, custom({1: 1.0})), (0.5, saw)), lambda x: np.round(x * 4) / 4),
    "DIGIT2": post(custom({1: 1.0}), lambda x: np.sin(3.2 * np.pi * x / 2) * 0.6 + 0.4 * np.sign(x) * np.abs(x) ** 0.3),
}

# inharmonic loops: partials as multiples of the fundamental (rounded to quarters: four cycles in the table)
INHARM = {
    "BELL": [(0.5, 0.15), (1.0, 1.0), (1.25, 0.35), (1.5, 0.3), (2.0, 0.45), (2.75, 0.3), (3.0, 0.2), (4.25, 0.15),
             (5.5, 0.08)],
    "BELL 2": [(1.0, 1.0), (2.0, 0.6), (3.0, 0.45), (4.25, 0.35), (5.5, 0.2), (6.75, 0.1)],
    "CHIME": [(1.0, 1.0), (2.75, 0.55), (5.5, 0.35), (9.0, 0.18), (13.25, 0.08)],
    "GLINT1": [(1.0, 1.0), (3.5, 0.4), (5.75, 0.3), (7.25, 0.25), (9.5, 0.15)],
    "GLINT2": [(1.0, 0.8), (4.25, 0.6), (6.5, 0.5), (8.75, 0.35), (11.25, 0.2), (13.5, 0.1)],
    "GLINT3": [(1.0, 0.6), (5.25, 0.7), (7.75, 0.5), (10.5, 0.45), (12.75, 0.3), (15.25, 0.2)],
    "ALIEN": [(abs(m - r), 0.6 / m) for m in (1, 2, 3, 4, 5) for r in (1.75,)] + [(m + 1.75, 0.5 / m) for m in (1, 2, 3, 4)],
    "METAL": [(1.0, 1.0), (1.5, 0.6), (2.25, 0.7), (2.75, 0.5), (3.5, 0.55), (4.25, 0.4), (5.0, 0.3), (6.25, 0.3),
              (7.75, 0.2), (9.25, 0.15)],
}
CYCLES = 4
# waves that stand for recorded instruments and voices: as quiet as recordings with headroom (their loudness, not
# their peaks, set alike), where the plain shapes use the whole range
SAMPLED = {"BASS", "BASS 2", "PIANO", "VOICE1", "VOICE2", "VOICE3", "REED", "REED 2", "REED 3", "BRASS", "STRING",
           "CLAV", "FORMT1", "FORMT2", "FORMT3", "FORMT4", "FORMT5", "SYNTH2"}

NOISES = ["NOISE1", "NOISE2", "NOISE3", "BREATH", "STEAM"]


def noise_table(name, n=4096):
    r = np.random.default_rng(NOISES.index(name) + 100)
    w = r.standard_normal(n)
    f = np.fft.rfftfreq(n, 1 / 38455.855)     # (heard at DOC rate at middle C)
    s = np.fft.rfft(w)
    if name == "NOISE1":
        g = np.ones_like(f)
    elif name == "NOISE2":
        g = 1 / np.sqrt(1 + (f / 1200) ** 2)
    elif name == "NOISE3":                      # metallic: combs
        g = 0.3 + sum(1 / (1 + ((f - c) / 120) ** 2) for c in (900, 2300, 3700, 5200, 7600))
    elif name == "BREATH":
        g = 1 / (1 + ((f - 1700) / 1100) ** 2) + 0.15 / (1 + (f / 400) ** 2)
    else:                                       # STEAM: hiss, high
        g = (f / 6000) ** 2 / (1 + (f / 6000) ** 2) / np.sqrt(1 + (f / 14000) ** 2)
    x = np.fft.irfft(s * g, n)
    x = np.tanh(1.6 * x / np.std(x) / 3) # (louder: the peaks rounded)
    return to_bytes(x, 124)


# ------------------------------------------------------------------------------------------------ one-shots
def env_exp(n, tau, attack=0.0):
    t = np.arange(n) / RS
    e = np.exp(-t / tau)
    if attack > 0:
        e *= np.minimum(1, t / attack)
    return e


def osc(freq, n, phase=0.0):
    """a sine with freq a number or an array (Hz per sample)"""
    f = np.broadcast_to(np.asarray(freq, float), (n,))
    return np.sin(2 * np.pi * np.cumsum(f) / RS + phase)


def noise(n, seed):
    return np.random.default_rng(seed).standard_normal(n)


def lp(x, fc):
    """a one-pole low-pass"""
    a = np.exp(-2 * np.pi * fc / RS)
    y = np.zeros_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc = (1 - a) * v + a * acc
        y[i] = acc
    return y


def hp(x, fc):
    return x - lp(x, fc)


def bp(x, fc, q=2.0):
    """a two-pole band-pass (state variable)"""
    f = 2 * np.sin(np.pi * fc / RS)
    low = band = 0.0
    y = np.zeros_like(x)
    for i, v in enumerate(x):
        low += f * band
        high = v - low - band / q
        band += f * high
        y[i] = band
    return y


def karplus(f0, n, damp=0.5, bright=0.5, seed=1):
    period = RS / f0
    p = int(round(period))
    buf = np.random.default_rng(seed).uniform(-1, 1, p)
    buf = lp(buf, 2000 + 9000 * bright)
    out = np.zeros(n)
    for i in range(n):
        j = i % p
        out[i] = buf[j]
        nxt = buf[(j + 1) % p]
        buf[j] = (buf[j] + nxt) * 0.5 * (0.9995 - 0.008 * damp)
    return out


def harmonic_tone(f0, n, amps, taus):
    t = np.arange(n) / RS
    x = np.zeros(n)
    for k, (a, tau) in enumerate(zip(amps, taus), start=1):
        if a and k * f0 < RS / 2:
            x += a * np.sin(2 * np.pi * k * f0 * t) * np.exp(-t / tau)
    return x


def fade_end(x, ms=4):
    m = min(len(x), int(RS * ms / 1000))
    x = x.copy()
    x[-m:] *= np.linspace(1, 0, m)
    return x


def fit(x, size):
    """x in a table of size samples (cut, or padded with silence), its end faded"""
    y = np.zeros(size)
    m = min(size, len(x))
    y[:m] = x[:m]
    return fade_end(y)


def drum_kick(n, f_hi=150, f_lo=48, tau=0.18, click=0.3):
    t = np.arange(n) / RS
    f = f_lo + (f_hi - f_lo) * np.exp(-t / 0.035)
    return osc(f, n) * np.exp(-t / tau) + click * hp(noise(n, 5), 2000) * np.exp(-t / 0.004)


def drum_snare(n, tone=190, tau=0.12, seed=7, snap=1.0):
    t = np.arange(n) / RS
    body = (osc(tone, n) + 0.5 * osc(tone * 1.6, n)) * np.exp(-t / 0.06)
    wires = hp(lp(noise(n, seed), 7000), 900) * np.exp(-t / tau) * snap
    return 0.6 * body + wires


def drum_tom(n, f0=130, tau=0.25):
    t = np.arange(n) / RS
    f = f0 * (1 + 0.35 * np.exp(-t / 0.05))
    return (osc(f, n) + 0.15 * osc(f * 1.5, n)) * np.exp(-t / tau) + 0.08 * noise(n, 9) * np.exp(-t / 0.01)


def drum_hat(n, tau=0.03, seed=11):
    t = np.arange(n) / RS
    sq = sum(np.sign(np.sin(2 * np.pi * f * t + i)) for i, f in enumerate((317, 421, 553, 607, 773, 911)))
    x = hp(sq + 0.8 * noise(n, seed), 6000)
    return x * np.exp(-t / tau)


def drum_clap(n, seed=13):
    t = np.arange(n) / RS
    e = np.zeros(n)
    for d in (0.0, 0.011, 0.022, 0.034):
        i = int(d * RS)
        e[i:] += np.exp(-(t[: n - i]) / (0.006 if d < 0.03 else 0.09))
    return bp(noise(n, seed), 1200, 1.5) * e


def drum_rim(n):
    t = np.arange(n) / RS
    return (osc(1700, n) + 0.6 * osc(830, n)) * np.exp(-t / 0.012) + 0.4 * hp(noise(n, 17), 3000) * np.exp(-t / 0.004)


def drum_cowbell(n):
    t = np.arange(n) / RS
    x = np.sign(np.sin(2 * np.pi * 562 * t)) + np.sign(np.sin(2 * np.pi * 845 * t))
    return bp(x, 900, 3) * (0.6 * np.exp(-t / 0.02) + 0.4 * np.exp(-t / 0.15))


def drum_conga(n, f0):
    t = np.arange(n) / RS
    f = f0 * (1 + 0.12 * np.exp(-t / 0.02))
    return osc(f, n) * np.exp(-t / 0.17) + 0.2 * bp(noise(n, 19), 2500, 2) * np.exp(-t / 0.008)


def drum_shaker(n):
    t = np.arange(n) / RS
    e = np.minimum(1, t / 0.015) * np.exp(-t / 0.05)
    return hp(noise(n, 23), 4500) * e


def drum_crash(n):
    t = np.arange(n) / RS
    sq = sum(np.sign(np.sin(2 * np.pi * f * t + i)) for i, f in enumerate((345, 467, 587, 683, 811, 997, 1201)))
    return hp(sq + 1.5 * noise(n, 29), 4000) * np.exp(-t / 0.35)


def drum_808(n):
    return drum_kick(n, f_hi=110, f_lo=46, tau=0.45, click=0.1)


def drum_tomlo(n): return drum_tom(n, 95, 0.3)
def drum_tomhi(n): return drum_tom(n, 190, 0.2)
def drum_ohat(n): return drum_hat(n, tau=0.18, seed=12)


def oneshot(name):
    """(samples, table size, its pitch's key: the key at which it plays at RS)"""
    f0 = C4
    if name == "BOWING":
        n = 8192
        t = np.arange(n) / RS
        scrape = bp(noise(n, 41), 2200, 1.2) * np.exp(-t / 0.08) * np.minimum(1, t / 0.01)
        tone = harmonic_tone(f0, n, [1 / k for k in range(1, 16)], [0.25] * 15) * np.minimum(1, t / 0.12)
        return fit(0.9 * scrape + 0.6 * tone, n), n
    if name == "PICK 1":
        n = 4096
        t = np.arange(n) / RS
        x = 0.8 * hp(noise(n, 43), 1500) * np.exp(-t / 0.004) + harmonic_tone(
            f0, n, [1 / k ** 0.6 for k in range(1, 30)], [0.09 / (1 + 0.12 * k) for k in range(1, 30)])
        return fit(x, n), n
    if name == "PICK 2":
        n = 4096
        x = 0.5 * hp(noise(n, 44), 800) * env_exp(n, 0.006) + karplus(f0, n, 0.4, 0.7, 45) * env_exp(n, 0.08)
        return fit(x, n), n
    if name == "MALLET":
        n = 4096
        return fit(harmonic_tone(f0, n, [1, 0, 0, 0.45, 0, 0, 0, 0, 0.12], [0.16, 1, 1, 0.05, 1, 1, 1, 1, 0.015])
                   + 0.15 * lp(noise(n, 47), 3000) * env_exp(n, 0.003), n), n
    if name == "SLAP":
        n = 4096
        t = np.arange(n) / RS
        x = harmonic_tone(f0 / 2, n, [1 / k ** 0.7 for k in range(1, 25)], [0.12 / (1 + 0.15 * k) for k in range(1, 25)])
        return fit(x + 0.6 * hp(noise(n, 49), 1200) * np.exp(-t / 0.005), n), n
    if name == "PLINK":
        n = 4096
        return fit(harmonic_tone(f0 * 2, n, [1, 0.5, 0.35, 0.2, 0.15, 0.1], [0.12, 0.07, 0.05, 0.035, 0.025, 0.02]), n), n
    if name == "PLUCK":
        n = 8192
        return fit(karplus(f0, n, 0.3, 0.75, 51), n), n
    if name == "PLUNK":
        n = 8192
        return fit(lp(karplus(f0 / 2, n, 0.7, 0.3, 53), 1800) * 1.5, n), n
    if name == "CLICK":
        n = 1024
        t = np.arange(n) / RS
        x = (osc(1900, n) + 0.6 * osc(3100, n)) * np.exp(-t / 0.004) + 0.5 * hp(noise(n, 55), 2500) * np.exp(-t / 0.002)
        return fit(x, n), n
    if name == "CHIFF":
        n = 4096
        t = np.arange(n) / RS
        x = bp(noise(n, 57), 2 * f0 * 4, 3.0) * np.exp(-t / 0.025) * np.minimum(1, t / 0.004)
        return fit(x + 0.3 * osc(f0 * 2, n) * np.exp(-t / 0.04), n), n
    if name == "THUMP":
        n = 4096
        t = np.arange(n) / RS
        f = f0 / 2 * (1 + 0.6 * np.exp(-t / 0.02))
        return fit((osc(f, n) + 0.3 * osc(2 * f, n)) * np.exp(-t / 0.06) + 0.2 * lp(noise(n, 59), 800) * np.exp(-t / 0.01),
                   n), n
    if name == "LOGDRM":
        n = 8192
        t = np.arange(n) / RS
        f = f0 * (1 + 0.08 * np.exp(-t / 0.03))
        return fit(osc(f, n) * np.exp(-t / 0.22) + 0.35 * osc(f * 2.72, n) * np.exp(-t / 0.08), n), n
    if name in SHARED:                       # (the kits' own sounds: kept once)
        fn, n = POOL[SHARED[name]]
        return fit(fn(n), n), n
    raise KeyError(name)


SHARED = {"KICK 2": "kick", "SNARE": "snare", "TOMTOM": "tom", "HI-HAT": "hat"}


# drum pool for DRUMS1-5 (sound, table size) and the kits: one sound for each of the 16 zones (keys 8z .. 8z+7)
POOL = {"kick": (drum_kick, 8192), "808": (drum_808, 8192), "snare": (drum_snare, 8192),
        "snare2": (lambda n: drum_snare(n, 230, 0.08, 31, 1.3), 4096), "rim": (drum_rim, 2048),
        "clap": (drum_clap, 4096), "tomlo": (drum_tomlo, 8192), "tom": (drum_tom, 8192), "tomhi": (drum_tomhi, 4096),
        "hat": (drum_hat, 2048), "ohat": (drum_ohat, 4096), "cowbell": (drum_cowbell, 4096),
        "congalo": (lambda n: drum_conga(n, 190), 4096), "congahi": (lambda n: drum_conga(n, 290), 4096),
        "shaker": (drum_shaker, 4096), "crash": (drum_crash, 8192)}
KITS = {
    "DRUMS1": ["kick"] * 6 + ["snare", "snare", "hat", "hat", "ohat", "tom", "tomhi", "clap", "crash", "crash"],
    "DRUMS2": ["tomlo"] * 6 + ["tomlo", "tom", "tom", "tomhi", "tomhi", "congalo", "congahi", "rim", "rim", "crash"],
    "DRUMS3": ["congalo"] * 6 + ["congalo", "congahi", "cowbell", "shaker", "shaker", "rim", "clap", "hat", "ohat", "crash"],
    "DRUMS4": ["808"] * 6 + ["808", "snare2", "clap", "hat", "ohat", "cowbell", "rim", "tomlo", "tom", "crash"],
    "DRUMS5": ["kick"] * 6 + ["808", "snare2", "snare", "rim", "clap", "hat", "ohat", "shaker", "cowbell", "crash"],
}


# ------------------------------------------------------------------------------------------------ the pack
class Pack:
    def __init__(self):
        self.rom = bytearray()
        self.raws = []                  # (start, size_log2, resolution, tune semitones float)
        self.where = {}                 # a table's bytes -> its start (the same table is kept once)
        self.made = {}                  # (start, tune) -> its raw

    def table(self, data, tune):
        data = bytes(data)
        size = len(data)
        lg = size.bit_length() - 1
        assert size == 1 << lg and 256 <= size <= 32768, size
        tune = round(float(tune), 3)
        if data in self.where:          # kept already: the same raw, or another at another tuning
            start = self.where[data]
            if (start, tune) in self.made:
                return self.made[(start, tune)]
        else:
            while len(self.rom) % 256:
                self.rom.append(128)
            start = len(self.rom)
            self.rom += data
            self.where[data] = start
        res = 1                         # (a slower pass a resolution step up: an octave, for tunes past -120)
        while tune < -120 and res < 7:
            res += 1
            tune += 12
        self.raws.append((start, lg, res, tune))
        assert len(self.raws) <= 255, "more than 255 tables"
        self.made[(start, round(float(tune), 3) if res == 1 else ("r", res, tune))] = len(self.raws) - 1
        return len(self.raws) - 1


def oneshot_tune(size, key=60):
    """tune (semitones) for a table of size samples to play at RS at key: one pass = size / RS seconds"""
    want = RS / size                      # passes a second at key
    return 12 * np.log2(want / (hz(key) * 2 ** (1 - 1))) + 0


def build():
    pk = Pack()
    zones = []
    cache = {}
    for name in [" ".join(n.split()) for n in NAMES]:
        if name in LOOPED or name == "SINE":
            z = [0] * 16
            for rz, hmax, key in RANGES:
                h = 1 if name == "SINE" else hmax
                a, p = LOOPED[name](h, hz(key))
                raw = pk.table(to_bytes(from_harmonics(a[:h], None if p is None else p[:h]),
                                        rms=40.0 if name in SAMPLED else None), 0.0)
                for zz in rz:
                    z[zz] = raw
                if name == "SINE":   # (one table does)
                    z = [raw] * 16
                    break
            zones.append(z)
        elif name in INHARM:
            z = [0] * 16
            for rz, hmax, key in RANGES[:3] + [RANGES[3]]:
                n = 1024
                t = np.arange(n) / n
                x = np.zeros(n)
                for ratio, amp in INHARM[name]:
                    k = int(round(ratio * CYCLES))
                    if 0 < k and k <= CYCLES * hmax and k < n // 2:
                        x += amp * np.sin(2 * np.pi * k * t + k * 0.7)
                raw = pk.table(to_bytes(x), -12 * np.log2(CYCLES))
                for zz in rz:
                    z[zz] = raw
            zones.append(z)
        elif name in NOISES:
            raw = pk.table(noise_table(name), 12 * np.log2(DOC / 4096 / hz(60)) + 0)
            zones.append([raw] * 16)
        elif name in KITS:
            kit, z, zz = KITS[name], [], 0
            while zz < 16:                   # each run of zones with one sound: natural at the run's middle key
                e = zz
                while e + 1 < 16 and kit[e + 1] == kit[zz]:
                    e += 1
                snd = kit[zz]
                if snd not in cache:
                    fn, size = POOL[snd]
                    y = fit(fn(size), size)
                    cache[snd] = (to_bytes(np.tanh(2.5 * y / np.max(np.abs(y))), 118), size)   # (drums: punch)
                data, size = cache[snd]
                raw = pk.table(data, oneshot_tune(size, (8 * zz + 8 * e + 8) // 2))
                z += [raw] * (e - zz + 1)
                zz = e + 1
            zones.append(z)
        else:
            x, size = oneshot(name)
            if name in SHARED and SHARED[name] in cache:
                data, size = cache[SHARED[name]]
            else:
                data = to_bytes(np.tanh(2.5 * x / np.max(np.abs(x))) if name in SHARED else x, 118)
                if name in SHARED:
                    cache[SHARED[name]] = (data, size)
            raw = pk.table(data, oneshot_tune(size))
            zones.append([raw] * 16)
    # the ROM: a power of two
    size = 4096
    while size < len(pk.rom):
        size *= 2
    rom = bytes(pk.rom) + bytes([128]) * (size - len(pk.rom))
    names = "".join(NAMES).encode("ascii")
    zt = bytes(b for z in zones for b in z)
    rawb = bytearray()
    for start, lg, res, tune in pk.raws:
        semi = int(np.floor(tune))
        frac = int(round((tune - semi) * 256))
        if frac == 256:
            semi, frac = semi + 1, 0
        assert -128 <= semi <= 127
        rawb += bytes([(start >> 8) & 0xFF, (start >> 16) << 6 | (lg - 8) << 3 | res, semi & 0xFF, frac])
    body = names + zt + bytes(rawb)
    body += bytes((16 - len(body) % 16) % 16)
    head = b"SQ80PACK" + struct.pack("<IIIIII", 1, 32, 75, len(pk.raws), size, 0)
    return head + body + rom, len(pk.raws), len(pk.rom)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("--header", help="also a C header with the pack as const data")
    a = ap.parse_args()
    pack, nraw, used = build()
    Path(a.out).write_bytes(pack)
    print(f"{a.out}: {len(pack)} bytes, {nraw} tables, wave ROM {used} bytes used", file=sys.stderr)
    if a.header:
        lines = ["/* SPDX-License-Identifier: MIT", " * SQ-80's built-in waves: an original set, made by tools/gen_waves.py "
                 "(no Ensoniq data). Generated: do not edit. */", "#pragma once", "#include <stdint.h>",
                 f"#define SQ80_WAVES_SIZE {len(pack)}u",
                 "static const uint8_t SQ80_WAVES_PACK[SQ80_WAVES_SIZE] __attribute__((aligned(4))) = {"]
        for i in range(0, len(pack), 32):
            lines.append(",".join(str(b) for b in pack[i:i + 32]) + ",")
        lines.append("};")
        Path(a.header).write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
