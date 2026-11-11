#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check an M-Vave SMK-37 full flash backup (two reads, jl-uboot-tool "read 0 0x100000 ..."), and say what the stock
firmware keeps in the stretches SQ-80 would like to use. Prints no unit-unique data: only sizes, whether the two copies
match, the calibration marker, and for each stretch whether it is empty (all 0xFF) or how many bytes are written.

    python3 tools/smk_dump_check.py smk37-dump-1.bin smk37-dump-2.bin
"""
import hashlib
import sys

STRETCHES = [(0x9D000, 0xBB000, "store window (VM)"), (0xC2000, 0xEB000, "USRFLASH"),
             (0xEB000, 0xF4000, "unlisted 0xEB000-0xF3FFF"), (0xFE000, 0xFF000, "unlisted 0xFE000")]


def main(a, b):
    d1, d2 = open(a, "rb").read(), open(b, "rb").read()
    ok = True
    for name, d in ((a, d1), (b, d2)):
        if len(d) != 0x100000:
            print(f"FAIL {name}: {len(d)} bytes, not 1048576: read it again")
            ok = False
    if hashlib.sha256(d1).digest() != hashlib.sha256(d2).digest():
        print("FAIL the two reads differ: read again")
        ok = False
    if d1[0xC1224:0xC122B] != b"trimdat":
        print("FAIL no 'trimdat' at 0xC1224: do not trust this dump (or the calibration is missing)")
        ok = False
    if not ok:
        return 1
    print("backup OK: 1 MiB, both reads identical, calibration marker present. Keep both files in two places.")
    for lo, hi, what in STRETCHES:
        used = sum(1 for x in d1[lo:hi] if x != 0xFF)
        print(f"  {what:28s} {'empty (all 0xFF)' if not used else f'{used} bytes written'}")
    print("  sectors 0xEA000-0xF3FFF (bytes written in each; 0 = empty):")
    print("   " + " ".join(f"{a >> 12:02X}:{sum(1 for x in d1[a:a + 0x1000] if x != 0xFF)}"
                           for a in range(0xEA000, 0xF4000, 0x1000)))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2]))
