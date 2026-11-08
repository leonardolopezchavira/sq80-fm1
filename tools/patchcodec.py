"""ESQ-1 / SQ-80 program (102 bytes) <-> dict, and SysEx framing.

Byte map: the SQ-80 SysEx program layout as documented in the SQ8L port's MIT-licensed
docs/modules/program.md ("SysEx (SQ-80 / ESQ-1) mapping"), cross-checked against 5,343 unique
programs from real banks. See docs/patch-format.md.
"""
SOURCES = "LFO1 LFO2 LFO3 ENV1 ENV2 ENV3 ENV4 VEL VELX KBD KBD2 WHEEL PEDAL XCTRL PRESS OFF".split()

def s8(b): return b - 256 if b >= 128 else b
def s7(b): b &= 0x7F; return b - 128 if b >= 64 else b
def half(b):  # signed byte / 2, rounding toward zero
    v = s8(b); return int(v / 2)

def decode(p):
    p = bytes(p); assert len(p) == 102
    d = {"name": p[0:6].decode("latin1")}
    d["env"] = []
    for e in range(4):
        b = p[6 + 10 * e: 16 + 10 * e]
        d["env"].append(dict(L1=half(b[0]), L2=half(b[1]), L3=half(b[2]),
                             T1=b[3] & 63, T2=b[4] & 63, T3=b[5] & 63, T4=b[6] & 63,
                             R2=bool(b[6] & 0x80), LV=b[7] >> 2, T1V=b[8] & 63, TK=b[9] & 63))
    d["lfo"] = []
    for l in range(3):
        b = p[0x2E + 4 * l: 0x32 + 4 * l]
        d["lfo"].append(dict(FREQ=b[0] & 63, WAVE=b[0] >> 6, L1=b[1] & 63, L2=b[2] & 63,
                             MOD=((b[1] >> 6) << 2) | (b[2] >> 6), DELAY=b[3] & 63,
                             HUMAN=bool(b[3] & 0x40), RESET=bool(b[3] & 0x80)))
    d["osc"] = []
    for o in range(3):
        b = p[0x3A + 10 * o: 0x44 + 10 * o]
        d["osc"].append(dict(OCT=(b[0] & 0x7F) // 12 - 3, SEMI=(b[0] & 0x7F) % 12, FINE=b[1] >> 3,
                             MOD1=b[2] & 15, MOD2=b[2] >> 4, DEPTH1=half(b[3]), DEPTH2=half(b[4]),
                             WAVE=b[5], LEVEL=(b[6] & 0x7F) >> 1, OUTPUT=bool(b[6] & 0x80),
                             DMOD1=b[7] & 15, DMOD2=b[7] >> 4, DDEPTH1=half(b[8]), DDEPTH2=half(b[9])))
    d["dca4"] = dict(FINAL=(p[0x58] & 0x7F) >> 1, AM=bool(p[0x58] & 0x80))
    d["filter"] = dict(FREQ=p[0x59] & 0x7F, RES=p[0x5A] & 31, MOD1=p[0x5B] & 15, MOD2=p[0x5B] >> 4,
                       DEPTH1=s7(p[0x5C]), DEPTH2=s7(p[0x5D]), KEYBD=(p[0x5E] & 0x7F) >> 1)
    d["mode"] = dict(SYNC=bool(p[0x59] & 0x80), VC=bool(p[0x5C] & 0x80), MONO=bool(p[0x5D] & 0x80),
                     ENV=bool(p[0x5E] & 0x80), OSC=bool(p[0x5F] & 0x80), GLIDE=p[0x5F] & 0x7F,
                     CYC=bool(p[0x65] & 0x80))
    d["pan"] = dict(PAN=p[0x64] >> 4, MOD=p[0x64] & 15, DEPTH=s7(p[0x65]))
    return d

def u7(v): return v & 0x7F
def dbl(v): return (v * 2) & 0xFF

def encode(d, base=None):
    """Write the decoded fields over `base` (a 102-byte program, default all zero)."""
    p = bytearray(base if base is not None else bytes(102))
    p[0:6] = d["name"].encode("latin1")[:6].ljust(6)
    for e, v in enumerate(d["env"]):
        o = 6 + 10 * e
        p[o:o + 10] = bytes([dbl(v["L1"]), dbl(v["L2"]), dbl(v["L3"]), v["T1"], v["T2"], v["T3"],
                             v["T4"] | (0x80 if v["R2"] else 0), (v["LV"] << 2) | (p[o + 7] & 3), v["T1V"], v["TK"]])
    for l, v in enumerate(d["lfo"]):
        o = 0x2E + 4 * l
        p[o:o + 4] = bytes([v["FREQ"] | (v["WAVE"] << 6), v["L1"] | ((v["MOD"] >> 2) << 6),
                            v["L2"] | ((v["MOD"] & 3) << 6),
                            v["DELAY"] | (0x40 if v["HUMAN"] else 0) | (0x80 if v["RESET"] else 0)])
    for i, v in enumerate(d["osc"]):
        o = 0x3A + 10 * i
        p[o:o + 10] = bytes([(v["OCT"] + 3) * 12 + v["SEMI"], (v["FINE"] << 3) | (p[o + 1] & 7), v["MOD1"] | (v["MOD2"] << 4),
                             dbl(v["DEPTH1"]), dbl(v["DEPTH2"]), v["WAVE"],
                             (v["LEVEL"] << 1) | (0x80 if v["OUTPUT"] else 0), v["DMOD1"] | (v["DMOD2"] << 4),
                             dbl(v["DDEPTH1"]), dbl(v["DDEPTH2"])])
    m, f = d["mode"], d["filter"]
    p[0x58] = (d["dca4"]["FINAL"] << 1) | (0x80 if d["dca4"]["AM"] else 0)
    p[0x59] = f["FREQ"] | (0x80 if m["SYNC"] else 0)
    p[0x5A] = f["RES"]
    p[0x5B] = f["MOD1"] | (f["MOD2"] << 4)
    p[0x5C] = u7(f["DEPTH1"]) | (0x80 if m["VC"] else 0)
    p[0x5D] = u7(f["DEPTH2"]) | (0x80 if m["MONO"] else 0)
    p[0x5E] = (f["KEYBD"] << 1) | (0x80 if m["ENV"] else 0)
    p[0x5F] = m["GLIDE"] | (0x80 if m["OSC"] else 0)
    p[0x64] = (d["pan"]["PAN"] << 4) | d["pan"]["MOD"]
    p[0x65] = u7(d["pan"]["DEPTH"]) | (0x80 if m["CYC"] else 0)
    return bytes(p)

def programs_from_syx(data):
    """All 102-byte programs in a .syx file (single-program and 40-program bank dumps)."""
    out, i = [], 0
    while True:
        s = data.find(b"\xf0\x0f\x02", i)
        if s < 0: break
        e = data.find(b"\xf7", s)
        if e < 0: break
        msg = data[s:e]
        if len(msg) > 5 and msg[4] in (1, 2):
            body = msg[5:]
            nyb = [(body[k] & 15) | ((body[k + 1] & 15) << 4) for k in range(0, len(body) - 1, 2)]
            out += [bytes(nyb[j:j + 102]) for j in range(0, len(nyb) - 101, 102)]
        i = e + 1
    return out

def single_syx(program, channel=0):
    return bytes([0xF0, 0x0F, 0x02, channel, 0x01]) + bytes(b for x in program for b in (x & 15, x >> 4)) + b"\xf7"

def basic(name="TEST"):
    """A neutral test program: OSC1 SINE at full level, filter open, ENV4 full sustain."""
    flat = dict(L1=63, L2=63, L3=63, T1=0, T2=0, T3=0, T4=0, R2=False, LV=0, T1V=0, TK=0)
    zero = dict(flat, L1=0, L2=0, L3=0)
    osc = dict(OCT=0, SEMI=0, FINE=0, MOD1=15, MOD2=15, DEPTH1=0, DEPTH2=0, WAVE=2, LEVEL=63,
               OUTPUT=True, DMOD1=15, DMOD2=15, DDEPTH1=0, DDEPTH2=0)
    return dict(name=name, env=[dict(zero), dict(zero), dict(zero), dict(flat)],
                lfo=[dict(FREQ=0, WAVE=0, L1=0, L2=0, MOD=15, DELAY=0, HUMAN=False, RESET=False)] * 3,
                osc=[osc, dict(osc, OUTPUT=False, LEVEL=0), dict(osc, OUTPUT=False, LEVEL=0)],
                dca4=dict(FINAL=63, AM=False),
                filter=dict(FREQ=127, RES=0, MOD1=15, MOD2=15, DEPTH1=0, DEPTH2=0, KEYBD=0),
                mode=dict(SYNC=False, VC=False, MONO=False, ENV=False, OSC=False, GLIDE=0, CYC=False),
                pan=dict(PAN=8, MOD=15, DEPTH=0))
