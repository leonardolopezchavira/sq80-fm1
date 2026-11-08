#!/usr/bin/env python3
"""Build an SQ-80 ROM pack from your own ROM dumps.

The pack holds the wave directory (wave names, the 16-zone multisample table and the raw-wave
table, read from the SQ-80 OS ROM) and the 256 KiB wave ROM. No Ensoniq data is part of this
repository: you supply the dumps, the pack is for your own use.

    make_rompack.py --os-low sq80rom.low --os-high sq80rom.hig \
        --wave 2202.bin 2204.bin 2203.bin 2205.bin -o sq80.rompack
    # or a single 256 KiB SQ80m voice ROM:  --wave "SQ80m voice rom 020.bin"

Format (little endian), see docs/rompack.md:
    0   "SQ80PACK" magic, u32 version (1), u32 header size (32), u32 wave count (75),
        u32 raw count, u32 wave ROM size
    32  75 x 6-byte names, 75 x 16 multisample bytes, raw count x 4 bytes, padded to 16
    ..  wave ROM (banks 0..3 = chips 2202, 2204, 2203, 2205)
"""
import argparse, struct, sys

NAMES_AT, MS_AT, RAW_AT, NWAVES = 0x63B9, 0x1000, 0x14B0, 75

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--os-low", required=True)
    ap.add_argument("--os-high", required=True)
    ap.add_argument("--wave", nargs="+", required=True,
                    help="2202 2204 2203 2205 (that order) or one 256 KiB image")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()

    osr = open(a.os_low, "rb").read() + open(a.os_high, "rb").read()
    if len(osr) != 0x10000:
        sys.exit("the OS ROM halves must be 32 KiB each")
    names = osr[NAMES_AT:NAMES_AT + 6 * NWAVES]
    if names[:6].strip() != b"SAW" or names[-6:].strip() != b"DRUMS5":
        sys.exit("this does not look like an SQ-80 OS ROM (wave names not found)")
    ms = osr[MS_AT:MS_AT + 16 * NWAVES]
    nraw = max(ms) + 1
    raw = osr[RAW_AT:RAW_AT + 4 * nraw]

    wave = b"".join(open(p, "rb").read() for p in a.wave)
    if len(wave) != 0x40000:
        sys.exit("the wave ROM must total 256 KiB (2202, 2204, 2203, 2205)")
    if wave[0x400:0x404] == wave[0x10400:0x10404] == wave[0x20400:0x20404]:
        print("warning: the wave chips look identical; check the files and their order")

    body = names + ms + raw
    body += b"\0" * (-len(body) % 16)
    header = b"SQ80PACK" + struct.pack("<5I", 1, 32, NWAVES, nraw, len(wave)) + b"\0" * 4
    open(a.out, "wb").write(header + body + wave)
    print(f"wrote {a.out}: {NWAVES} waves, {nraw} raw waves, {len(header + body + wave)} bytes")

if __name__ == "__main__":
    main()
