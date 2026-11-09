# SQ-80

SQ-80 is an unofficial Ensoniq SQ-80 for the M-VAVE FM-1, written from scratch: three wavetable oscillators,
a resonant 4-pole filter, four envelopes and three LFOs per voice, eight voices on the FM-1's two cores. It is
measured against the SQ8L emulation to sound like the real instrument.

It plays **your own** SQ-80 waves and programs: none are included. Make a ROM pack from your SQ-80 ROM dumps
(`tools/make_rompack.py` in the source); the [installer](install/) then asks for it and your ESQ-1 / SQ-80 SysEx
banks and loads them in the same go as the firmware, and remembers them in your browser for the next install.
Up to eight banks of 40 programs. The [loader](load/) loads them on their own. Without a ROM pack it plays a
small synthetic test wave set.

This is version {{VERSION}}. You install it at your own risk, and the installer can put M-VAVE's own firmware back.

- [Install](install/) it from Chrome or Edge, with the FM-1 connected by USB.
- [Load](load/) your ROM pack and programs (Chrome or Edge).
- [Try it in the browser](emu/) (with the test waves).
- [Download {{PKG}}](firmware/{{PKG}}).
- The [Source](https://github.com/leonardolopezchavira/sq80-fm1): the firmware is GPL-3.0-only (the engine MIT). It's
  built on fm1-base (MPL-2.0) and felucca-bsp, which come from Felucca by Leo Kuroshita, Hügelton Instruments; its
  effects are FMini-1's, by Ruppert, after Felucca's; its arpeggiator, sequencer and recording are SLOOP's.

On the FM-1: SELECT picks the bank (1–8: every one has 40 programs, INIT where you have not loaded or saved any),
PRESET the program, K1–K4 change cutoff, resonance,
attack and release (HOME puts them back), OCT- / OCT+ the octave. Every value of a program can be edited: SEL for
the oscillators, ENV for the envelopes, LFO for the LFOs, EDIT for the filter, output, modes and name (press again
for the next one, ALGO for the pages, K1–K4 for the values, HOME to play); SAVE asks where (SELECT or K1 the bank, PRESET or K2
the program, it shows what it replaces) and SAVE again stores it. FX opens the effects, FMini-1's: reverb, delay, chorus and drive, their levels on the first page
and each one's TYPE and settings on the next (FX again, or ALGO); they keep themselves. ARP turns SLOOP's arpeggiator on and
off (14 modes, 1/4 to 1/32 and triplets, 1–4 octaves, gate, swing, hold, order, chance); hold ARP for its pages.
SEQ opens the step sequencer: on its STEP page the notes you play are written into the steps one after another
(a chord when held together; K1 moves the cursor, K2–K4 the step's notes, tie / rest and accent / slide), and ALGO
goes on to length (up to 64 steps), division, gate, swing, tempo and MIDI clock; hold SEQ and
the white keys are the steps (an empty one is set with the last note or chord played, a set one tapped is cleared,
held with K1–K4 its notes, velocity, ratchets and type), the first black keys the pages. PLAY starts and stops it,
REC records what you play into it (stopped: the first note starts it), REC held 2 s clears it, EDIT + OCT- / OCT+
undo and redo. HOME's screens: playing, with the output's waveform under the knobs; the waveform alone, the
program's name at the foot; RANDOM, new programs from the one playing (K1 how far, K2 which parts, K3 a new one or
the one before, K4 back to the stored one; SAVE keeps one you like). GLO shows the audio load, and its K1 sets the metronome click: off, while recording, or always. MIDI in
over USB, Bluetooth and TRS plays it, program changes too, and an ESQ-1 / SQ-80 single-program dump sent to it
plays at once.

Ensoniq, SQ-80, M-VAVE and FM-1 are trademarks of their owners. This project is not affiliated with any of them.
