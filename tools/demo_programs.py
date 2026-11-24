#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The demo video's programs (tools/demo_video.py): made here from scratch, on SQ-80's own 75 waves (no one else's
programs, no Ensoniq data). Each is a dict for tools/patchcodec.py, encoded to the 102 bytes of an ESQ-1 / SQ-80
program.

    python3 tools/demo_programs.py OUT.syx      # them as a 40-program bank (the rest INIT): for the Load page too
"""
import copy
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import patchcodec as pc  # noqa: E402

# wave slots (tools/gen_waves.py NAMES) and modulation sources (sq80_params.c)
W = dict(SAW=0, BELL=1, SINE=2, SQUARE=3, PULSE=4, BASS=8, PIANO=9, EPNO=10, VOICE1=11, VOICE2=12, REED=14, ORGAN=15,
         SYNTH1=16, SYNTH2=17, FORMT2=20, PULSE2=24, SQR2=25, BASS2=28, EPNO2=29, OCTAVE=30, SAW2=32, TRIANG=33,
         GLINT1=39, CLAV=42, BRASS=43, STRING=44, BELL2=47, BREATH=49, VOICE3=50, CHIME=53, BOWING=54, PLUCK=60)
LFO1, LFO2, LFO3, ENV1, ENV2, ENV3, ENV4, VEL, VELX, KBD, KBD2, WHEEL, PEDAL, XCTRL, PRESS, OFF = range(16)


def program(name):
    d = copy.deepcopy(pc.basic(name))
    d["lfo"] = [dict(x) for x in d["lfo"]]     # (basic() shares one LFO dict among the three)
    for o in d["osc"]:
        o.update(WAVE=W["SAW"], LEVEL=0, OUTPUT=False)
    return d


def osc(d, i, wave, level=55, oct_=0, semi=0, fine=0, **mods):
    d["osc"][i].update(WAVE=W[wave], LEVEL=level, OUTPUT=True, OCT=oct_, SEMI=semi, FINE=fine, **mods)


def env(d, i, l1, l2, l3, t1, t2, t3, t4, lv=0, t1v=0, tk=0):
    d["env"][i].update(L1=l1, L2=l2, L3=l3, T1=t1, T2=t2, T3=t3, T4=t4, LV=lv, T1V=t1v, TK=tk)


def programs():
    out = []

    # a wide pad: two detuned saws and the string wave an octave up, the filter opening slowly, a slow vibrato
    d = program("SQPAD")
    osc(d, 0, "SAW2", 52, MOD1=LFO1, DEPTH1=2)
    osc(d, 1, "SAW2", 52, fine=7, MOD1=LFO1, DEPTH1=-2)
    osc(d, 2, "STRING", 38, oct_=1)
    d["lfo"][0].update(FREQ=18, WAVE=0, L1=20, L2=20, MOD=OFF)
    d["filter"].update(FREQ=52, RES=6, KEYBD=30, MOD1=ENV3, DEPTH1=28, MOD2=VEL, DEPTH2=8)
    env(d, 2, 50, 63, 40, 30, 32, 30, 34)
    env(d, 3, 63, 63, 58, 24, 20, 20, 38, lv=10)
    out.append(d)

    # bells: the bell wave with a chime an octave and a fifth up, struck, ringing out
    d = program("BELLS")
    osc(d, 0, "BELL", 55)
    osc(d, 1, "CHIME", 42, oct_=1, semi=7)
    osc(d, 2, "SINE", 36, oct_=-1)
    d["filter"].update(FREQ=96, RES=0, KEYBD=40, MOD1=ENV3, DEPTH1=20)
    env(d, 2, 63, 30, 0, 0, 28, 30, 30)
    env(d, 3, 63, 44, 0, 0, 34, 44, 40, lv=30)
    out.append(d)

    # plucked: the pluck wave over a short saw, for the arpeggiator
    d = program("PLUCKS")
    osc(d, 0, "PLUCK", 63)
    osc(d, 1, "SAW", 60, fine=4)
    osc(d, 2, "TRIANG", 52, oct_=-1)
    d["filter"].update(FREQ=80, RES=12, KEYBD=40, MOD1=ENV3, DEPTH1=36, MOD2=VEL, DEPTH2=10)
    env(d, 2, 63, 26, 10, 0, 22, 24, 20)
    env(d, 3, 63, 58, 34, 0, 28, 30, 26, lv=6)
    out.append(d)

    # a bass for the sequencer: the bass wave and a square an octave down, a snappy filter
    d = program("SQBASS")
    osc(d, 0, "BASS2", 63)
    osc(d, 1, "SQR2", 52, oct_=-1)
    d["filter"].update(FREQ=52, RES=18, KEYBD=20, MOD1=ENV3, DEPTH1=44, MOD2=VEL, DEPTH2=10)
    env(d, 2, 63, 16, 0, 0, 16, 20, 14)
    env(d, 3, 63, 60, 54, 0, 18, 20, 14, lv=6)
    out.append(d)

    # a mono lead: square and detuned saw, glide, a delayed vibrato
    d = program("GLIDER")
    osc(d, 0, "SQR2", 60, MOD1=LFO1, DEPTH1=3)
    osc(d, 1, "SAW2", 54, fine=9, MOD1=LFO1, DEPTH1=3)
    osc(d, 2, "SAW", 30, oct_=-1)
    d["lfo"][0].update(FREQ=24, WAVE=0, L1=0, L2=28, DELAY=30, MOD=OFF)
    d["filter"].update(FREQ=76, RES=12, KEYBD=40, MOD1=ENV3, DEPTH1=24)
    d["mode"].update(MONO=True, GLIDE=26)
    d["dca4"]["FINAL"] = 55
    env(d, 2, 63, 40, 30, 4, 24, 24, 20)
    env(d, 3, 63, 63, 60, 2, 20, 20, 22, lv=6)
    out.append(d)

    # voices: two vocal waves, slow in, for the RANDOM part to start from
    d = program("VOXPAD")
    osc(d, 0, "VOICE1", 54)
    osc(d, 1, "FORMT2", 46, fine=5)
    osc(d, 2, "VOICE3", 36, oct_=1)
    d["filter"].update(FREQ=70, RES=4, KEYBD=30, MOD1=LFO2, DEPTH1=6)
    d["lfo"][1].update(FREQ=10, WAVE=0, L1=30, L2=30, MOD=OFF)
    env(d, 3, 63, 63, 60, 28, 20, 20, 36, lv=8)
    out.append(d)
    return out


def encoded():
    return [bytes(pc.encode(d)) for d in programs()]


if __name__ == "__main__":
    progs = encoded()
    init = bytes(pc.encode(pc.basic("INIT")))
    allp = progs + [init] * (40 - len(progs))
    body = []
    for p in allp:
        for b in p:
            body += [b & 15, b >> 4]
    Path(sys.argv[1]).write_bytes(bytes([0xF0, 0x0F, 0x02, 0x00, 0x02] + body + [0xF7]))
    print(f"{sys.argv[1]}: {len(progs)} demo programs")
