# SQ-80

SQ-80 is an unofficial Ensoniq SQ-80 for the M-VAVE FM-1, written from scratch: three wavetable oscillators,
a resonant 4-pole filter, four envelopes and three LFOs per voice, eight voices on the FM-1's two cores. It is
measured against the SQ8L emulation to sound like the real instrument.

It comes with a set of 75 waves of its own, made from scratch in the SQ-80's slots and names (no Ensoniq data),
so it plays from the start. If you own an SQ-80 or its ROMs you can **optionally** make a ROM pack from your
dumps for the original waves ([make it here](rompack/) from the four wave ROMs and the OS ROM, in your browser): the [installer](install/) asks for it and
your ESQ-1 / SQ-80 SysEx banks and loads them in the same go as the firmware (and remembers them in your browser
for the next install), the [loader](load/) loads them on their own, and the emulator takes them too. Up to eight
banks of 40 programs.

This is version {{VERSION}}. You install it at your own risk, and the installer can put M-VAVE's own firmware back.

- [Install](install/) it from Chrome or Edge, with the FM-1 connected by USB.
- Read the [manual](manual/): every screen, with pictures.
- [Load](load/) your ROM pack and programs (Chrome or Edge).
- [Make your ROM pack](rompack/) from your SQ-80's ROM dumps (in the browser: nothing is uploaded).
- [Make a wave pack](wavepack/) from your own sounds (WAV, AIFF, FLAC, MP3): into any of the 75 wave slots.
- [Edit programs](edit/): every value, heard as you change it, in the page and live on the FM-1 (Chrome or Edge).
- [Try it in the browser](emu/) (its own waves; "Your ROM pack and banks…" there loads yours).
- [Download {{PKG}}](firmware/{{PKG}}).
- The [Source](https://github.com/leonardolopezchavira/sq80-fm1): the firmware is GPL-3.0-only (the engine MIT). It's
  built on fm1-base (MPL-2.0) and felucca-bsp, which come from Felucca by Leo Kuroshita, Hügelton Instruments; its
  effects are FMini-1's, by Ruppert, after Felucca's; its arpeggiator, sequencer and recording are SLOOP's.

Ensoniq, SQ-80, M-VAVE and FM-1 are trademarks of their owners. This project is not affiliated with any of them.
