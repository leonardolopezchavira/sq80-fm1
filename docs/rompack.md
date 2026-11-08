# ROM pack format (version 1)

Built by `tools/make_rompack.py` from the user's own SQ-80 OS ROM (low + high halves) and the
four wave ROMs 2202, 2204, 2203, 2205 (or one 256 KiB image). Little endian.

| Offset | Size | Content |
|---|---|---|
| 0 | 8 | `SQ80PACK` |
| 8 | 4 | version = 1 |
| 12 | 4 | header size = 32 |
| 16 | 4 | wave count = 75 |
| 20 | 4 | raw wave count (151) |
| 24 | 4 | wave ROM size = 0x40000 |
| 28 | 4 | 0 |
| 32 | 450 | wave names, 6 characters each (OS ROM 0x63B9) |
| 482 | 1200 | multisample table: 16 raw-wave indices per wave, lowest zone first (OS 0x1000) |
| 1682 | 4 x raw | raw waves: page, wave select register (bank 7-6, size 5-3, resolution 2-0), tune semitones (signed), tune 1/256 (OS 0x14B0) |
| ... | | zero padding to a multiple of 16 |
| ... | 262144 | wave ROM, banks 0-3 |

Total 264,432 bytes for the SQ-80 (151 raw waves): it fits the FM-1's 436 KB plain flash window
(0x93000-0x100000).
