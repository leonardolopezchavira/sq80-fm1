"""Measure the SQ-80 filter through the oracle (black box): noise (NOISE1) through the filter,
spectrum relative to the open filter. Prints JSON with the cutoff (-6 dB point) per FREQ, the
resonance peak per RES, key tracking and envelope-to-cutoff depth.
"""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__)); sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from sq8l_oracle import Oracle
import patchcodec as pc

SR = 44100
ENGINE = os.environ.get("ENGINE")  # path to sq80_render: measure this engine instead of the oracle
ROMPACK = os.environ.get("ROMPACK")
o = None if ENGINE else Oracle()

def play(prog, key):
    if ENGINE:
        from compare import ours
        return ours(ROMPACK, ENGINE, prog, key, 127, 1.4, 1.4)
    l, _ = o.render(prog, [(0.0, key, 127, 1.4)], 1.4)
    return l

def psd(x):
    seg = 4096
    w = np.hanning(seg)
    frames = [x[i:i + seg] * w for i in range(int(0.2 * SR), len(x) - seg, seg // 2)]
    return np.mean([np.abs(np.fft.rfft(f)) ** 2 for f in frames], axis=0)

F = np.fft.rfftfreq(4096, 1 / SR)

def render(freq=127, res=0, key=60, keybd=0, env_depth=None, wave=5):
    d = pc.basic("FILT")
    d["osc"][0]["WAVE"] = wave
    d["filter"].update(FREQ=freq, RES=res, KEYBD=keybd)
    if env_depth is not None:  # ENV3 held at +63 -> filter MOD1
        d["env"][2] = dict(L1=63, L2=63, L3=63, T1=0, T2=0, T3=0, T4=0, R2=False, LV=0, T1V=0, TK=0)
        d["filter"].update(MOD1=pc.SOURCES.index("ENV3"), DEPTH1=env_depth)
    return play(pc.encode(d), key)

open_ref = {}
def response(**kw):
    key = kw.get("key", 60)
    if key not in open_ref:
        open_ref[key] = psd(render(freq=127, key=key))
    return 10 * np.log10(psd(render(**kw)) / open_ref[key] + 1e-20)

def cutoff_hz(resp):
    """Lowest frequency (above 30 Hz) where the response falls 6 dB below its low-frequency level."""
    band = (F > 30) & (F < 18000)
    base = np.median(resp[(F > 30) & (F < 60)])
    idx = np.nonzero(band & (resp < base - 6))[0]
    return float(F[idx[0]]) if len(idx) else 18000.0

if __name__ == "__main__":
    out = {"cutoff": {}, "resonance": {}, "keytrack": {}, "envdepth": {}}
    for f in range(0, 128, 4):
        out["cutoff"][f] = cutoff_hz(response(freq=f))
        print("FREQ", f, round(out["cutoff"][f]), flush=True)
    for r in (0, 8, 16, 24, 31):
        resp = response(freq=64, res=r)
        band = (F > 50) & (F < 16000)
        out["resonance"][r] = dict(peak_db=float(resp[band].max()), peak_hz=float(F[band][np.argmax(resp[band])]))
        print("RES", r, out["resonance"][r], flush=True)
    for keybd in (0, 32, 63):
        for key in (12, 24, 36, 48, 60):
            out["keytrack"][f"{keybd}/{key}"] = cutoff_hz(response(freq=0, keybd=keybd, key=key))
            print("KEYBD", keybd, "key", key, round(out["keytrack"][f"{keybd}/{key}"]), flush=True)
    for dep in (0, 16, 32, -16, 63):
        out["envdepth"][dep] = cutoff_hz(response(freq=40, env_depth=dep))
        print("ENV3 depth", dep, round(out["envdepth"][dep]), flush=True)
    json.dump(out, open(sys.argv[1] if len(sys.argv) > 1 else "filter.json", "w"), indent=1)
