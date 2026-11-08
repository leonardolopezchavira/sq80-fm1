# SQ-80

SQ-80 is an unofficial Ensoniq SQ-80 for the M-VAVE FM-1, written from scratch: three wavetable oscillators,
a resonant 4-pole filter, four envelopes and three LFOs per voice, eight voices on the FM-1's two cores. It is
measured against the SQ8L emulation to sound like the real instrument.

It plays **your own** SQ-80 waves and programs: none are included. Make a ROM pack from your SQ-80 ROM dumps
(`tools/make_rompack.py` in the source), then load it and your ESQ-1 / SQ-80 SysEx banks with the
[loader](load/). Until then it plays a small synthetic test wave set and four test sounds.

This is version {{VERSION}}. You install it at your own risk, and the installer can put M-VAVE's own firmware back.

- [Install](install/) it from Chrome or Edge, with the FM-1 connected by USB.
- [Load](load/) your ROM pack and programs (Chrome or Edge).
- [Try it in the browser](emu/) (with the test waves).
- [Download {{PKG}}](firmware/{{PKG}}).
- The [Source](https://github.com/leonardolopezchavira/sq80-fm1) is under MIT. It's built on fm1-base (MPL-2.0)
  and felucca-bsp, which come from Felucca by Leo Kuroshita, Hügelton Instruments.

On the FM-1: SELECT picks the bank (T: the test sounds), PRESET the program, K1–K4 change cutoff, resonance,
attack and release (HOME puts them back), OCT- / OCT+ the octave, SAVE keeps bank, program and octave. MIDI in
over USB, Bluetooth and TRS plays it, program changes too, and an ESQ-1 / SQ-80 single-program dump sent to it
plays at once.

Ensoniq, SQ-80, M-VAVE and FM-1 are trademarks of their owners. This project is not affiliated with any of them.
