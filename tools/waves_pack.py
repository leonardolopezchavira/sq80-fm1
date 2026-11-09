#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The built-in waves (app/waves/sq80_waves.h) back into a .rompack file: the wave pack maker's base (site/wavepack).

    waves_pack.py app/waves/sq80_waves.h OUT.rompack
"""
import re, sys
text = open(sys.argv[1]).read()
body = text[text.index("{", text.index("SQ80_WAVES_PACK")) + 1:text.rindex("}")]
data = bytes(int(v) for v in re.findall(r"\d+", body))
size = int(re.search(r"SQ80_WAVES_SIZE (\d+)u", text).group(1))
assert len(data) == size and data[:8] == b"SQ80PACK", "not the pack"
open(sys.argv[2], "wb").write(data)
print(f"{sys.argv[2]}: {len(data)} bytes")
