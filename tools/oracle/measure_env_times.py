"""Measure the SQ-80 envelope segment time for every TIME value 0..63 (black box, via the oracle).

ENV4 (the amplifier) goes from L1 = 63 to L2 = 0 in T2 = t with T1 = 0, on a held SINE note.
The segment ends when the output is exactly silent. Result: seconds per full-scale segment.
"""
import sys, json, os
import numpy as np
sys.path.insert(0, os.path.dirname(__file__)); sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from sq8l_oracle import Oracle
import patchcodec as pc

o = Oracle()
sr = o.sr
res = {}
TICK = 1.0 / 83.592575
for t in range(64):
    d = pc.basic("ENVT")
    d["env"][3] = dict(L1=63, L2=0, L3=0, T1=0, T2=t, T3=0, T4=0, R2=False, LV=0, T1V=0, TK=0)
    prog = pc.encode(d)
    secs = min(60.0, 0.2 + 2.2 * 2 ** ((t + 3) / 6) * TICK)
    l, _ = o.render(prog, [(0.0, 69, 127, secs)], secs)
    # amplitude per control tick (peak of |x| over each tick), then the first tick at the floor
    n = int(TICK * sr)
    a = np.array([np.abs(l[i:i + n]).max() for i in range(0, len(l) - n, n)])
    floor = a[-5:].max()
    k = int(np.argmax(a <= floor * 1.02 + 1e-9))
    res[t] = k * TICK
    print(t, k, flush=True)
json.dump(res, open(sys.argv[1] if len(sys.argv) > 1 else "env_times.json", "w"), indent=1)
