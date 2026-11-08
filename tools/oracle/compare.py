"""Render the same program and note through this engine (sq80_render) and the SQ8L oracle,
and report how they differ: pitch, loudness over time and spectrum.

    SQ8L_TESTAPI=... python3 compare.py ROMPACK RENDER_BIN program.syx|-  INDEX KEY [--wave N]
"""
import os, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(__file__)); sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from sq8l_oracle import Oracle
import patchcodec as pc

SR = 44100

def ours(rompack, render_bin, program, key, vel, hold, total):
    with tempfile.TemporaryDirectory() as d:
        syx = os.path.join(d, "p.syx"); open(syx, "wb").write(pc.single_syx(program))
        raw = os.path.join(d, "o.f32")
        subprocess.run([render_bin, rompack, syx, "0", os.path.join(d, "o.wav"), str(key), str(vel),
                        str(hold), "--raw", raw], check=True, capture_output=True)
        x = np.fromfile(raw, np.float32)
        n = len(x) // 2
        return x[:n][:int(total * SR)]

def f0(x):
    """Fundamental by autocorrelation (40 Hz .. 4 kHz)."""
    x = x - x.mean()
    if np.abs(x).max() < 1e-6: return 0.0
    c = np.fft.irfft(np.abs(np.fft.rfft(x, 2 * len(x))) ** 2)[:len(x)]
    lo, hi = SR // 4000, SR // 40
    k = lo + int(np.argmax(c[lo:hi]))
    if 0 < k < len(c) - 1:  # parabolic refinement
        a, b, cc = c[k - 1], c[k], c[k + 1]
        den = a - 2 * b + cc
        if den < 0:
            k = k + max(-0.5, min(0.5, 0.5 * (a - cc) / den))
    return SR / k

def envelope(x, hop=441):
    return np.array([np.sqrt(np.mean(x[i:i + hop] ** 2)) for i in range(0, len(x) - hop, hop)])

def spectrum(x):
    w = np.hanning(len(x))
    s = np.abs(np.fft.rfft(x * w)) + 1e-9
    f = np.fft.rfftfreq(len(x), 1 / SR)
    bands = np.geomspace(50, 16000, 33)
    return np.array([20 * np.log10(np.mean(s[(f >= a) & (f < b)])) for a, b in zip(bands[:-1], bands[1:])])

def compare(oracle, rompack, render_bin, program, key, vel=100, hold=1.0, total=1.6):
    a, _ = oracle.render(program, [(0.0, key, vel, hold)], total)
    b = ours(rompack, render_bin, program, key, vel, hold, total)
    n = min(len(a), len(b)); a, b = a[:n], b[:n]
    seg = slice(int(0.15 * SR), int(0.55 * SR))
    ea, eb = envelope(a), envelope(b)
    sa, sb = spectrum(a[seg]), spectrum(b[seg])
    return dict(f0_ref=f0(a[seg]), f0_ours=f0(b[seg]),
                level_db=20 * np.log10((eb.max() + 1e-9) / (ea.max() + 1e-9)),
                env_corr=float(np.corrcoef(ea, eb)[0, 1]) if ea.std() > 0 and eb.std() > 0 else float("nan"),
                spec_diff_db=float(np.mean(np.abs((sa - sa.max()) - (sb - sb.max())))))
