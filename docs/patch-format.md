# ESQ-1 / SQ-80 program format (102 bytes)

SysEx: single program `F0 0F 02 <ch> 01 <204 nibbles> F7`, bank `F0 0F 02 <ch> 02 <40 x 204
nibbles> F7`; each byte is sent as two nibbles, low nibble first.

Byte map (from the MIT-licensed SQ8L port documentation, checked against 5,343 real programs):

| Bytes | Content |
|---|---|
| 0-5 | name |
| 6 + 10e | ENV e+1: L1 L2 L3 (signed byte / 2), T1 T2 T3 T4 (6 bit; T4 bit 7 = second release "R"), LV (>> 2), T1V, TK |
| 0x2e + 4l | LFO l+1: b0 FREQ(6) + WAVE(2: TRI SAW SQR NOISE); b1 L1(6) + MOD bits 3-2; b2 L2(6) + MOD bits 1-0; b3 DELAY(6) + HUMAN (bit 6) + RESET (bit 7) |
| 0x3a + 10o | OSC o+1: b0 octave*12+semitone (octave = b0/12 - 3); b1 FINE << 3; b2 pitch mod sources (low nibble MOD1, high MOD2); b3/b4 pitch mod depths (signed / 2); b5 WAVE; b6 DCA LEVEL << 1 + OUTPUT (bit 7); b7 DCA mod sources; b8/b9 DCA mod depths |
| 0x58 | DCA4 level << 1, AM (bit 7) |
| 0x59 | FILTER FREQ (7 bit), SYNC (bit 7) |
| 0x5a | RES (5 bit) |
| 0x5b | filter mod sources |
| 0x5c / 0x5d | filter MOD1 / MOD2 depth (7-bit signed); bit 7 = RESTART VC / MONO |
| 0x5e | KEYBD << 1, RESTART ENV (bit 7) |
| 0x5f | GLIDE (7 bit), RESTART OSC (bit 7) |
| 0x64 | pan mod source (low nibble), PAN (high nibble) |
| 0x65 | pan mod depth (7-bit signed), CYC (bit 7) |

Modulation sources: 0-2 LFO1-3, 3-6 ENV1-4, 7 VEL, 8 VEL-X, 9 KBD, 10 KBD2, 11 WHEEL,
12 PEDAL, 13 XCTRL, 14 PRESS, 15 OFF.
