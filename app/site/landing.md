# SQ-80 TEST

SQ-80 TEST is a test firmware for the M-VAVE FM-1. It runs sq80-fm1's engine, an unofficial Ensoniq SQ-80
voice written from scratch (three wavetable oscillators, a resonant 4-pole filter, four envelopes, three LFOs,
eight voices), on a small set of synthetic waves built into the firmware. It holds no Ensoniq data.

It is here to answer one question: how much of the FM-1's CPU the engine takes. The screen shows the audio load
now and its peak. PRESET picks one of four test sounds, OCT- and OCT+ the octave, ARP holds all eight voices
at once (the worst case), HOME resets the peak, and SAVE keeps the sound and the octave. MIDI in over USB,
Bluetooth and TRS plays it too.

This is version {{VERSION}}. You install it at your own risk, and the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/). It runs the same code as the FM-1 (the CPU load shown there is your computer's).
- [Install](install/) it from Chrome or Edge, with the FM-1 connected by USB.
- [Download {{PKG}}](firmware/{{PKG}}).
- The [Source](https://github.com/leonardolopezchavira/sq80-fm1) is under MIT. It's built on fm1-base (MPL-2.0)
  and felucca-bsp, which come from Felucca by Leo Kuroshita, Hügelton Instruments.

Ensoniq, SQ-80, M-VAVE and FM-1 are trademarks of their owners. This project is not affiliated with any of them.
