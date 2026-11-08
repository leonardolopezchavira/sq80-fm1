"""Measure modulation depths and level curves (black box). ENGINE=path/to/sq80_render measures
this engine instead of the oracle. Prints JSON."""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__)); sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import patchcodec as pc
from compare import ours, f0

SR = 44100
ENGINE, ROMPACK = os.environ.get("ENGINE"), os.environ.get("ROMPACK")
if not ENGINE:
    from sq8l_oracle import Oracle
    o = Oracle()

def play(d, key=60, vel=127, hold=1.0, total=1.2, stereo=False):
    prog = pc.encode(d)
    if ENGINE:
        x = ours(ROMPACK, ENGINE, prog, key, vel, hold, total) if not stereo else None
        if stereo:
            import subprocess, tempfile
            with tempfile.TemporaryDirectory() as t:
                syx = os.path.join(t, "p.syx"); open(syx, "wb").write(pc.single_syx(prog))
                raw = os.path.join(t, "o.f32")
                subprocess.run([ENGINE, ROMPACK, syx, "0", os.path.join(t, "o.wav"), str(key), str(vel), str(hold), "--raw", raw], check=True, capture_output=True)
                y = np.fromfile(raw, np.float32); n = len(y) // 2
                return y[:n][:int(total * SR)], y[n:][:int(total * SR)]
        return x
    l, r = o.render(prog, [(0.0, key, vel, hold)], total)
    return (l, r) if stereo else l

FLAT = dict(L1=63, L2=63, L3=63, T1=0, T2=0, T3=0, T4=0, R2=False, LV=0, T1V=0, TK=0)
seg = slice(int(0.3 * SR), int(0.9 * SR))
rms = lambda x: float(20 * np.log10(np.sqrt(np.mean(x[seg] ** 2)) + 1e-12))
out = {}

# pitch: ENV1 held at +63 -> OSC1 pitch MOD1 depth d (SINE)
base = f0(play(pc.basic("P"))[seg])
out["pitch_env"] = {}
for dep in (2, 8, 16, 32, 63, -16):
    d = pc.basic("P"); d["env"][0] = dict(FLAT)
    d["osc"][0].update(MOD1=pc.SOURCES.index("ENV1"), DEPTH1=dep)
    out["pitch_env"][dep] = 12 * np.log2(f0(play(d)[seg]) / base)

# vibrato: LFO1 TRI, FREQ 10, L1 = L2 = 63 -> pitch depth 63; peak deviation in semitones
d = pc.basic("V"); d["lfo"][0] = dict(FREQ=10, WAVE=0, L1=63, L2=63, MOD=15, DELAY=0, HUMAN=False, RESET=True)
d["osc"][0].update(MOD1=0, DEPTH1=32)
x = play(d, total=2.2, hold=2.0)
hop = 4410
pitches = [f0(x[i:i + hop]) for i in range(int(0.2 * SR), int(2.0 * SR) - hop, hop)]
p = 12 * np.log2(np.array(pitches) / base)
out["vibrato_depth32_lfo63"] = float((p.max() - p.min()) / 2)

# DCA level curve: OSC1 LEVEL L
out["dca_level"] = {}
for L in (63, 56, 48, 40, 32, 24, 16, 8, 1):
    d = pc.basic("L"); d["osc"][0]["LEVEL"] = L
    out["dca_level"][L] = rms(play(d))
# DCA level modulation: LEVEL 32 + ENV1 (+63) x depth
out["dca_mod"] = {}
for dep in (8, 16, 31, -16):
    d = pc.basic("M"); d["osc"][0].update(LEVEL=32, DMOD1=pc.SOURCES.index("ENV1"), DDEPTH1=dep); d["env"][0] = dict(FLAT)
    out["dca_mod"][dep] = rms(play(d))
# DCA4 final level
out["dca4"] = {}
for L in (63, 48, 32, 16):
    d = pc.basic("F"); d["dca4"]["FINAL"] = L
    out["dca4"][L] = rms(play(d))
# velocity: ENV4 LV = 63 at velocities
out["velocity"] = {}
for v in (127, 96, 64, 32, 8):
    d = pc.basic("V"); d["env"][3]["LV"] = 63
    out["velocity"][v] = rms(play(d, vel=v))
# pan
out["pan"] = {}
for P in (0, 4, 8, 12, 15):
    d = pc.basic("PN"); d["pan"]["PAN"] = P
    l, r = play(d, stereo=True)
    out["pan"][P] = float(20 * np.log10((np.sqrt(np.mean(r[seg] ** 2)) + 1e-12) / (np.sqrt(np.mean(l[seg] ** 2)) + 1e-12)))
out["pan_env"] = {}
for dep in (16, 32, -32):
    d = pc.basic("PM"); d["pan"].update(PAN=8, MOD=pc.SOURCES.index("ENV1"), DEPTH=dep); d["env"][0] = dict(FLAT)
    l, r = play(d, stereo=True)
    out["pan_env"][dep] = float(20 * np.log10((np.sqrt(np.mean(r[seg] ** 2)) + 1e-12) / (np.sqrt(np.mean(l[seg] ** 2)) + 1e-12)))
json.dump(out, open(sys.argv[1], "w"), indent=1, default=float)
print(json.dumps(out, default=float))
