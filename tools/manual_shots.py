#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The manual's pictures of the FM-1's screen (app/docs/img/*.png), from the host simulator: a script that goes
through every screen with a program of our own (GLASS), the simulator run on it, each shot doubled in size.

    python3 fm1/base/tools/build.py app --host && python3 tools/manual_shots.py
"""
import subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import patchcodec as pc
from fm1_loader import frame, hexline
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "app" / "docs" / "img"
TMP = ROOT / "app" / "build" / "manual"

d = pc.basic("GLASS ")
o = d["osc"]
o[0].update(WAVE=0, LEVEL=55)
o[1].update(WAVE=0, LEVEL=50, OUTPUT=True, FINE=6)
d["filter"].update(FREQ=70, RES=8)
d["env"][3].update(T1=6, T2=20, T3=30, T4=28, L2=50, L3=40)
prog = pc.encode(d)
nib = [b for x in prog for b in (x & 15, x >> 4)]

S = ["wait 700",
     "sysex " + hexline(frame(0x30, nib)), "wait 100", "sysex_out",
     "key 7 down", "key 11 down", "key 14 down", "wait 400", "shot play.png",
     "tap HOME", "wait 200", "shot scope.png",
     "tap HOME", "wait 200", "shot random.png",
     "key 7 up", "key 11 up", "key 14 up",
     "tap HOME", "wait 100",
     "tap SEL", "wait 100", "shot osc.png",
     "tap ENV", "wait 100", "tap ENV", "tap ENV", "tap ENV", "wait 100", "shot env.png",
     "tap LFO", "wait 100", "shot lfo.png",
     "tap EDIT", "wait 100", "shot filter.png",
     "tap SAVE", "wait 100", "shot save.png", "tap HOME", "wait 100",
     "tap FX", "wait 100", "shot fx.png", "tap FX", "wait 100", "shot fx_reverb.png",
     "tap HOME", "wait 100",
     "press ARP", "wait 700", "release ARP", "wait 100", "shot arp.png",
     "tap SEQ", "wait 100"]
for k in (0, 7, 4, 11, 2, 9, 4, 0):      # step entry: a little phrase into steps 1-8
    S += [f"key {k} down", "wait 40", f"key {k} up", "wait 40"]
S += ["wait 100", "shot seq_step.png",
      "press SEQ", "wait 60", "key 6 down", "wait 100", "shot seq_held.png", "key 6 up", "wait 60", "release SEQ", "wait 100",
      "turn ALGO 1", "wait 100", "shot seq_pattern.png",
      "tap HOME", "wait 100", "tap PLAY", "wait 700", "shot transport.png", "tap PLAY", "wait 200",
      "tap GLO", "wait 400", "shot system.png", "expect splash 1", "shot splash.png"]

TMP.mkdir(parents=True, exist_ok=True)
(TMP / "manual.txt").write_text("\n".join(S) + "\n")
r = subprocess.run([str(ROOT / "app/build/host/sq80_host"), str(TMP / "manual.txt"), str(TMP)], capture_output=True, text=True)
if "FAIL" in r.stdout:
    sys.exit(r.stdout)
OUT.mkdir(parents=True, exist_ok=True)
for p in sorted(TMP.glob("*.png")):
    Image.open(p).resize((480, 480), Image.NEAREST).save(OUT / p.name, optimize=True)
    print("img", p.name)
