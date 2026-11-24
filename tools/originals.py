#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""SQ-80's factory bank: 34 programs made from scratch for SQ-80, on its own 75 waves (tools/gen_waves.py), for dark
electronic music: industrial, synth-pop, darkwave, downtempo and trip-hop. In the spirit of those styles, not copies
of anyone's sounds. After the demo video's six (tools/demo_programs.py) they fill the bank of 40 the firmware carries.

    python3 tools/originals.py OUT.syx [--list LIST.md] [--header app/waves/sq80_programs.h]

Every program is a dict for tools/patchcodec.py. The FM-1's effects are not part of a program (FX is global)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import demo_programs as dp  # noqa: E402
import patchcodec as pc  # noqa: E402
from demo_programs import ENV1, ENV2, ENV3, ENV4, KBD, LFO1, LFO2, LFO3, OFF, PRESS, VEL, WHEEL, env, osc, program  # noqa: E402,F401

# the rest of the waves by name (gen_waves.py's slots)
dp.W.update(FORMT2=20, KICK2=66, NOISE1=5, NOISE2=6, NOISE3=7, KICK=13, SYNTH3=18, FORMT1=19, FORMT3=21, FORMT4=22, FORMT5=23, FOUROCT=26,
            PRIME=27, OCT5=31, REED2=34, REED3=35, GRIT1=36, GRIT2=37, GRIT3=38, GLINT2=40, GLINT3=41, DIGIT1=45,
            DIGIT2=46, ALIEN=48, STEAM=51, METAL=52, PICK1=55, PICK2=56, MALLET=57, SLAP=58, PLINK=59, PLUNK=61,
            CLICK=62, CHIFF=63, THUMP=64, LOGDRM=65, SNARE=67, HIHAT=69)
TRI, SAWL, SQR, NOISE = 0, 1, 2, 3     # LFO waves


def lfo(d, i, freq, wave=TRI, l1=0, l2=None, delay=0, mod=OFF, human=False, reset=False):
    d["lfo"][i].update(FREQ=freq, WAVE=wave, L1=l1, L2=l1 if l2 is None else l2, DELAY=delay, MOD=mod, HUMAN=human,
                       RESET=reset)


def flt(d, freq, res=0, keybd=32, mod1=OFF, depth1=0, mod2=OFF, depth2=0):
    d["filter"].update(FREQ=freq, RES=res, KEYBD=keybd, MOD1=mod1, DEPTH1=depth1, MOD2=mod2, DEPTH2=depth2)


# (name, style, what it is for) and the program
SOUNDS = []


def sound(style, what):
    def reg(fn):
        SOUNDS.append((style, what, fn))
        return fn
    return reg


# ================================================================================ industrial
@sound("industrial", "a grinding bass: saw and grit, the filter slammed open by velocity")
def _():
    d = program("GRINDR")
    osc(d, 0, "SAW", 63)
    osc(d, 1, "GRIT1", 58, fine=6)
    osc(d, 2, "SQUARE", 54, oct_=-1)
    flt(d, 40, 20, 20, ENV3, 44, VEL, 14)
    env(d, 2, 63, 24, 10, 0, 14, 20, 12)
    env(d, 3, 63, 58, 54, 0, 16, 20, 10, lv=18)
    d["mode"].update(MONO=True)
    return d


@sound("industrial", "a screaming sync lead: OSC 2 synced to OSC 1, its pitch swept by ENV 2; the wheel adds more")
def _():
    d = program("SCREAM")
    osc(d, 0, "SAW", 40)
    osc(d, 1, "SAW", 63, oct_=1, MOD1=ENV2, DEPTH1=30, MOD2=WHEEL, DEPTH2=20)
    osc(d, 2, "GRIT2", 34, fine=10)
    d["mode"].update(SYNC=True, MONO=True, GLIDE=12)
    flt(d, 70, 16, 40, ENV3, 24)
    env(d, 1, 63, 30, 18, 0, 26, 30, 20)
    env(d, 2, 63, 40, 30, 0, 22, 24, 18)
    env(d, 3, 63, 60, 56, 0, 16, 20, 18, lv=12)
    return d


@sound("industrial", "a struck metal hit, ringing and clangy, for percussion on the high keys")
def _():
    d = program("ANVIL")
    osc(d, 0, "METAL", 63)
    osc(d, 1, "CLAV", 56, semi=6)
    osc(d, 2, "NOISE2", 36, oct_=2)
    flt(d, 100, 10, 50, ENV3, 24)
    env(d, 2, 63, 10, 0, 0, 18, 20, 12)
    env(d, 3, 63, 50, 0, 0, 30, 38, 30, lv=8)
    return d


@sound("industrial", "rusted machinery: grit and noise drifting under a slow, uneven filter")
def _():
    d = program("RUSTED")
    osc(d, 0, "GRIT2", 52)
    osc(d, 1, "GRIT3", 46, fine=14, MOD1=LFO2, DEPTH1=2)
    osc(d, 2, "NOISE3", 30)
    lfo(d, 0, 6, TRI, 34, human=True)
    lfo(d, 1, 14, NOISE, 16)
    flt(d, 50, 18, 20, LFO1, 30, ENV3, 18)
    env(d, 2, 0, 63, 40, 38, 30, 30, 36)
    env(d, 3, 63, 63, 60, 34, 20, 20, 40)
    return d


@sound("industrial", "static and shortwave: noise through a resonant filter that a fast random LFO jumps around")
def _():
    d = program("STATIC")
    osc(d, 0, "NOISE1", 60)
    osc(d, 1, "DIGIT2", 30, oct_=2)
    lfo(d, 0, 46, NOISE, 50)
    flt(d, 52, 24, 30, LFO1, 30, WHEEL, 30)
    env(d, 3, 63, 63, 63, 8, 20, 20, 26)
    return d


@sound("industrial", "a soft, sad piano on its own: the piano wave, dull and close, with a long ring")
def _():
    d = program("ASHES")
    osc(d, 0, "PIANO", 60)
    osc(d, 1, "SINE", 24, oct_=1)
    flt(d, 66, 0, 40, ENV3, 14, VEL, 22)
    env(d, 2, 63, 30, 10, 0, 30, 34, 26)
    env(d, 3, 63, 46, 0, 0, 30, 46, 34, lv=30)
    return d


@sound("industrial", "a hammer hit: a thumping kick, metal and a burst of noise, for beats on the low keys")
def _():
    d = program("HAMMER")
    osc(d, 0, "KICK2", 63)
    osc(d, 1, "METAL", 40, oct_=-1)
    osc(d, 2, "NOISE1", 46)
    flt(d, 86, 8, 30, ENV3, 40, VEL, 16)
    env(d, 2, 63, 0, 0, 0, 10, 12, 8)
    env(d, 3, 63, 40, 0, 0, 22, 24, 16, lv=4)
    return d


# ================================================================================ synth-pop
@sound("synth-pop", "a tight sequencer bass: pulse and saw, a clicky filter envelope, made for 1/16 lines")
def _():
    d = program("POPBAS")
    osc(d, 0, "PULSE2", 63)
    osc(d, 1, "SAW", 56, fine=5)
    flt(d, 40, 16, 30, ENV3, 40, VEL, 12)
    env(d, 2, 63, 14, 4, 0, 12, 14, 8)
    env(d, 3, 63, 56, 52, 0, 14, 16, 8, lv=14)
    return d


@sound("synth-pop", "brass stabs: two detuned saws, the filter's attack a little slower than the sound's")
def _():
    d = program("STABS")
    osc(d, 0, "SAW", 54)
    osc(d, 1, "SAW", 54, fine=8)
    osc(d, 2, "BRASS", 34)
    flt(d, 36, 8, 30, ENV3, 40, VEL, 14)
    env(d, 2, 63, 38, 26, 6, 20, 24, 18)
    env(d, 3, 63, 58, 50, 0, 18, 20, 16, lv=14)
    return d


@sound("synth-pop", "a mallet line: marimba-like wood and a sine an octave up, short")
def _():
    d = program("MALLET")
    osc(d, 0, "MALLET", 63)
    osc(d, 1, "SINE", 44, oct_=1)
    flt(d, 96, 0, 40, VEL, 14)
    env(d, 3, 63, 46, 0, 0, 30, 34, 24, lv=8)
    return d


@sound("synth-pop", "a sampled-choir pad: two vocal waves, slow in, gently moving")
def _():
    d = program("CHOIRS")
    osc(d, 0, "VOICE1", 52, MOD1=LFO1, DEPTH1=2)
    osc(d, 1, "VOICE2", 50, fine=6, MOD1=LFO1, DEPTH1=-2)
    osc(d, 2, "FORMT3", 30, oct_=1)
    lfo(d, 0, 16, TRI, 18)
    flt(d, 72, 4, 30)
    env(d, 3, 63, 63, 58, 30, 20, 20, 36, lv=8)
    return d


@sound("synth-pop", "a bright pulse lead with vibrato on the wheel")
def _():
    d = program("PULSER")
    osc(d, 0, "PULSE", 62, MOD1=LFO1, DEPTH1=3)
    osc(d, 1, "SQR2", 54, fine=7, MOD1=LFO1, DEPTH1=3)
    lfo(d, 0, 28, TRI, 0, mod=WHEEL)
    flt(d, 80, 10, 40, ENV3, 16)
    env(d, 2, 63, 40, 30, 0, 22, 24, 18)
    env(d, 3, 63, 60, 56, 0, 18, 20, 18, lv=10)
    d["mode"].update(MONO=True, GLIDE=8)
    return d


@sound("synth-pop", "a big orchestral hit: strings and brass together, cut short")
def _():
    d = program("ORCHIT")
    osc(d, 0, "STRING", 58)
    osc(d, 1, "BRASS", 54, oct_=-1)
    osc(d, 2, "BELL2", 30, oct_=1)
    flt(d, 86, 6, 30, ENV3, 26)
    env(d, 2, 63, 30, 0, 0, 22, 26, 16)
    env(d, 3, 63, 46, 0, 0, 26, 30, 22, lv=16)
    return d


@sound("synth-pop", "a glassy bell pad: bells struck, a string held under them")
def _():
    d = program("GLASS")
    osc(d, 0, "BELL2", 50)
    osc(d, 1, "GLINT1", 40, oct_=1, DMOD1=ENV2, DDEPTH1=30)
    osc(d, 2, "STRING", 44)
    flt(d, 86, 2, 40)
    env(d, 1, 63, 0, 0, 0, 30, 30, 24)
    env(d, 3, 63, 54, 48, 6, 30, 30, 36, lv=14)
    return d


@sound("synth-pop", "a dark string section: two strings an octave apart, a slow swell")
def _():
    d = program("NOCTRN")
    osc(d, 0, "STRING", 56, MOD1=LFO1, DEPTH1=2)
    osc(d, 1, "BOWING", 48, oct_=-1, fine=5)
    lfo(d, 0, 20, TRI, 0, 18, delay=26)
    flt(d, 58, 4, 30, ENV3, 16)
    env(d, 2, 40, 63, 50, 34, 30, 30, 34)
    env(d, 3, 63, 63, 60, 32, 20, 20, 38, lv=8)
    return d


# ================================================================================ darkwave
@sound("darkwave", "a sick, seasick pad: detuned saws drifting against each other, random pitch wobble")
def _():
    d = program("NAUSEA")
    osc(d, 0, "SAW2", 52, MOD1=LFO1, DEPTH1=3)
    osc(d, 1, "SAW2", 52, fine=14, MOD1=LFO2, DEPTH1=-3)
    osc(d, 2, "GRIT1", 30, oct_=-1)
    lfo(d, 0, 9, TRI, 30, human=True)
    lfo(d, 1, 7, NOISE, 24)
    flt(d, 48, 14, 30, LFO1, 10)
    env(d, 3, 63, 63, 60, 24, 20, 20, 34, lv=8)
    return d


@sound("darkwave", "glitches: a fast random LFO throws the pitch about; hold a note and play with the wheel")
def _():
    d = program("GLITCH")
    osc(d, 0, "DIGIT1", 63, MOD1=LFO1, DEPTH1=24, MOD2=WHEEL, DEPTH2=20)
    osc(d, 1, "SQUARE", 52, oct_=1, MOD1=LFO1, DEPTH1=-18)
    lfo(d, 0, 50, NOISE, 40)
    flt(d, 92, 16, 30, LFO1, 16)
    env(d, 3, 63, 58, 54, 0, 20, 20, 16, lv=10)
    return d


@sound("darkwave", "a growling formant bass: vowel waves opening with the filter envelope")
def _():
    d = program("GROWL")
    osc(d, 0, "FORMT1", 63)
    osc(d, 1, "FORMT4", 58, fine=4)
    osc(d, 2, "SAW", 50, oct_=-1)
    flt(d, 42, 22, 20, ENV3, 38, LFO1, 8)
    lfo(d, 0, 34, TRI, 20)
    env(d, 2, 63, 30, 20, 2, 18, 20, 14)
    env(d, 3, 63, 56, 50, 0, 18, 20, 12, lv=16)
    d["mode"].update(MONO=True, GLIDE=10)
    return d


@sound("darkwave", "an air-raid siren: a slow, wide pitch swing over a hard square")
def _():
    d = program("SIREN")
    osc(d, 0, "SQUARE", 52, MOD1=LFO1, DEPTH1=40)
    osc(d, 1, "SAW", 44, fine=9, MOD1=LFO1, DEPTH1=40)
    lfo(d, 0, 14, TRI, 50)
    flt(d, 76, 12, 30)
    env(d, 3, 63, 63, 63, 10, 20, 20, 30)
    d["mode"].update(MONO=True)
    return d


@sound("darkwave", "a cold digital pad: digital waves an octave apart, the brightness breathing slowly")
def _():
    d = program("COLDPD")
    osc(d, 0, "DIGIT2", 50)
    osc(d, 1, "DIGIT1", 44, oct_=1, fine=5)
    osc(d, 2, "SINE", 36, oct_=-1)
    lfo(d, 0, 5, TRI, 26)
    flt(d, 56, 10, 30, LFO1, 22)
    env(d, 3, 63, 63, 58, 30, 20, 20, 38, lv=6)
    return d


@sound("darkwave", "a low drone: two octave waves a fifth apart, slowly beating, for underneath everything")
def _():
    d = program("DRONE")
    osc(d, 0, "OCTAVE", 56, MOD1=LFO1, DEPTH1=1)
    osc(d, 1, "OCT5", 50, fine=3)
    osc(d, 2, "FOUROCT", 34, oct_=-1)
    lfo(d, 0, 3, TRI, 20)
    lfo(d, 1, 2, TRI, 30)
    flt(d, 46, 16, 20, LFO2, 22)
    env(d, 3, 63, 63, 63, 40, 20, 20, 44)
    return d


# ================================================================================ downtempo
@sound("downtempo", "a string machine: detuned saws with a fast, shallow chorus-like vibrato, soft attack")
def _():
    d = program("STRMAC")
    osc(d, 0, "SAW2", 52, MOD1=LFO1, DEPTH1=2)
    osc(d, 1, "SAW2", 52, oct_=1, fine=6, MOD1=LFO2, DEPTH1=-2)
    lfo(d, 0, 34, TRI, 16)
    lfo(d, 1, 27, TRI, 16)
    flt(d, 64, 2, 30)
    env(d, 3, 63, 63, 60, 20, 20, 20, 32, lv=8)
    return d


@sound("downtempo", "a warm electric piano with a slow tremolo")
def _():
    d = program("WARMEP")
    osc(d, 0, "EPNO2", 60, DMOD1=LFO1, DDEPTH1=10)
    osc(d, 1, "SINE", 36, DMOD1=LFO1, DDEPTH1=-10)
    lfo(d, 0, 22, TRI, 30)
    flt(d, 74, 0, 40, VEL, 20)
    env(d, 3, 63, 50, 20, 0, 28, 36, 26, lv=26)
    return d


@sound("downtempo", "a round mono bass: triangle and sine, glide between notes")
def _():
    d = program("ROUNDB")
    osc(d, 0, "TRIANG", 62)
    osc(d, 1, "SINE", 50, oct_=-1)
    osc(d, 2, "SAW", 18)
    flt(d, 44, 6, 20, ENV3, 18)
    env(d, 2, 63, 30, 20, 0, 20, 20, 14)
    env(d, 3, 63, 60, 58, 0, 14, 20, 12, lv=10)
    d["mode"].update(MONO=True, GLIDE=22)
    return d


@sound("downtempo", "a soft sine-like lead, vibrato fading in, glide")
def _():
    d = program("SOFTLD")
    osc(d, 0, "TRIANG", 56, MOD1=LFO1, DEPTH1=4)
    osc(d, 1, "SINE", 50, oct_=1, MOD1=LFO1, DEPTH1=4)
    lfo(d, 0, 26, TRI, 0, 30, delay=34)
    flt(d, 80, 0, 40)
    env(d, 3, 63, 60, 56, 8, 20, 20, 24)
    d["mode"].update(MONO=True, GLIDE=20)
    return d


@sound("downtempo", "a robot voice: formant waves sawn through a resonant filter, for chords and lines")
def _():
    d = program("ROBOVX")
    osc(d, 0, "FORMT2", 62)
    osc(d, 1, "FORMT5", 56, fine=5)
    osc(d, 2, "PULSE", 40)
    flt(d, 78, 20, 40, LFO1, 14)
    lfo(d, 0, 18, TRI, 24)
    env(d, 3, 63, 60, 54, 4, 20, 20, 22, lv=12)
    return d


@sound("downtempo", "a dreamy sweep pad: the filter rises over seconds as you hold the chord")
def _():
    d = program("DREAMS")
    osc(d, 0, "SAW2", 50)
    osc(d, 1, "SYNTH2", 46, fine=6)
    osc(d, 2, "BREATH", 34, oct_=1)
    flt(d, 30, 14, 30, ENV3, 50)
    env(d, 2, 0, 63, 50, 44, 40, 40, 40)
    env(d, 3, 63, 63, 60, 30, 20, 20, 40, lv=6)
    return d


@sound("downtempo", "a vocoder-like choir: vowel and breath waves, a slow swell, the wheel opening it up")
def _():
    d = program("VOCODR")
    osc(d, 0, "VOICE3", 60, MOD1=LFO1, DEPTH1=1)
    osc(d, 1, "FORMT2", 52, fine=5)
    osc(d, 2, "BREATH", 30, oct_=1)
    lfo(d, 0, 18, TRI, 10)
    flt(d, 70, 10, 30, WHEEL, 40)
    env(d, 3, 63, 63, 58, 26, 20, 20, 38, lv=10)
    return d


# ================================================================================ trip-hop
@sound("trip-hop", "a deep sub bass: a sine and a quiet triangle, nothing above the low end")
def _():
    d = program("SUBBAS")
    osc(d, 0, "SINE", 63, oct_=-1)
    osc(d, 1, "TRIANG", 30, oct_=-1)
    flt(d, 40, 0, 10)
    env(d, 3, 63, 63, 60, 2, 20, 20, 14)
    d["mode"].update(MONO=True, GLIDE=6)
    return d


@sound("trip-hop", "a dub chord stab: a muted saw chord with a quick filter blip, for echoes (turn up the delay)")
def _():
    d = program("DUBSTB")
    osc(d, 0, "SAW", 56)
    osc(d, 1, "SQR2", 44, fine=5)
    flt(d, 62, 16, 30, ENV3, 30)
    env(d, 2, 63, 10, 0, 0, 16, 18, 10)
    env(d, 3, 63, 40, 0, 0, 24, 28, 16, lv=6)
    return d


@sound("trip-hop", "a plucked harp-like tone: pick and bell, gentle, for arpeggios")
def _():
    d = program("HARPSY")
    osc(d, 0, "PICK1", 63)
    osc(d, 1, "BELL", 44, oct_=1)
    osc(d, 2, "PLUCK", 58)
    flt(d, 92, 0, 40, VEL, 12)
    env(d, 3, 63, 54, 0, 0, 34, 42, 32, lv=6)
    return d


@sound("trip-hop", "dark keys: a muffled piano and an organ under it, slow and heavy")
def _():
    d = program("DARKKY")
    osc(d, 0, "PIANO", 58)
    osc(d, 1, "ORGAN", 34, oct_=-1)
    flt(d, 48, 4, 30, VEL, 20)
    env(d, 3, 63, 50, 30, 0, 30, 40, 28, lv=22)
    return d


@sound("trip-hop", "dusty vinyl air: filtered noise with a low sine, a bed under the beat")
def _():
    d = program("DUSTY")
    osc(d, 0, "NOISE2", 40)
    osc(d, 1, "SINE", 50, oct_=-1, DMOD1=LFO1, DDEPTH1=14)
    osc(d, 2, "BREATH", 34)
    lfo(d, 0, 4, TRI, 30)
    flt(d, 52, 8, 10, LFO1, 10)
    env(d, 3, 63, 63, 63, 26, 20, 20, 36)
    return d


@sound("trip-hop", "a lonely bell lead: a chime and sine, long tail, a little vibrato")
def _():
    d = program("LONELY")
    osc(d, 0, "CHIME", 54, MOD1=LFO1, DEPTH1=2)
    osc(d, 1, "SINE", 50, oct_=1, MOD1=LFO1, DEPTH1=2)
    lfo(d, 0, 24, TRI, 0, 22, delay=30)
    flt(d, 90, 0, 40)
    env(d, 3, 63, 44, 20, 0, 30, 40, 36, lv=20)
    return d


# FINAL (DCA 4) trimmed where a program came out louder than the rest (measured in the simulator, tools/originals.py's
# audit: the held ones near -31 dBFS RMS on a chord or line; the struck ones by their peak)
TRIM = dict(STATIC=44, STRMAC=48, WARMEP=49, SOFTLD=50, SUBBAS=46)


def originals():
    out = []
    for style, what, fn in SOUNDS:
        d = fn()
        if d["name"].strip() in TRIM:
            d["dca4"]["FINAL"] = TRIM[d["name"].strip()]
        out.append((style, what, d))
    return out


def bank():
    """the factory bank's 40: the demo video's six (tools/demo_programs.py), then the 34 originals"""
    return dp.encoded() + [bytes(pc.encode(d)) for _, _, d in originals()]


def listing():
    """(place, name, style, what) for the 40"""
    rows = [(d["name"].strip(), "demo", what) for d, what in zip(dp.programs(), DEMO_WHAT)]
    rows += [(d["name"].strip(), s, w) for s, w, d in originals()]
    return [(i + 1,) + r for i, r in enumerate(rows)]


DEMO_WHAT = ["a wide pad: two detuned saws and strings an octave up, the filter opening slowly",
             "bells: a bell and a chime an octave and a fifth up, struck, ringing out",
             "plucked: a pluck over a short saw, for the arpeggiator", "a sequencer bass: bass and square, a snappy filter",
             "a mono lead: square and detuned saw, glide, a delayed vibrato", "voices: two vocal waves, slow in"]


def syx(progs):
    body = []
    for p in progs:
        for b in p:
            body += [b & 15, b >> 4]
    return bytes([0xF0, 0x0F, 0x02, 0x00, 0x02] + body + [0xF7])


def header(progs):
    """app/waves/sq80_programs.h: the bank built into the firmware (app.c: bank 1 until a bank is stored there)"""
    out = ["/* SPDX-License-Identifier: MIT",
           " * SQ-80's factory bank: 40 programs of its own, made from scratch on its built-in waves by tools/originals.py",
           " * (no Ensoniq or anyone else's programs). Generated: do not edit. */",
           "#pragma once", "#include <stdint.h>", "#define SQ80_FACTORY_N 40u",
           "static const uint8_t SQ80_FACTORY[SQ80_FACTORY_N][102] = {"]
    for p, (n, name, style, _) in zip(progs, listing()):
        out.append(f"    /* {n:2d} {name:6s} {style} */")
        out.append("    {" + ",".join(str(b) for b in p) + "},")
    out.append("};")
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("syx", help="the bank as ESQ-1 / SQ-80 SysEx")
    ap.add_argument("--list", help="its program list (Markdown)")
    ap.add_argument("--header", help="the C header the firmware builds in (app/waves/sq80_programs.h)")
    a = ap.parse_args()
    o = originals()
    assert len(o) == 34, len(o)
    progs = bank()
    assert len(progs) == 40, len(progs)
    names = [r[1] for r in listing()]
    assert len(set(names)) == len(names), "duplicate names"
    Path(a.syx).write_bytes(syx(progs))
    if a.list:
        lines = ["| # | Name | Style | What it is |", "|---|---|---|---|"]
        lines += [f"| {n} | {name} | {s} | {w} |" for n, name, s, w in listing()]
        Path(a.list).write_text("\n".join(lines) + "\n")
    if a.header:
        Path(a.header).write_text(header(progs))
    print(f"{a.syx}: the factory bank, {len(progs)} programs")
