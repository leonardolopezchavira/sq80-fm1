# Licensing

- **The SQ-80 engine and the tools** (`src/sq80/`, `tools/` but `tools/gen_fx_tables.py`, `tests/`, the desktop
  renderer, `app/loader/`, `app/install_app.js`, `app/app.c`): MIT (`LICENSE`), our own code.
- **The effects** (`app/fx/`, `tools/gen_fx_tables.py`): GPL-3.0-only (`LICENSES/GPL-3.0.txt`). They are FMini-1's
  (https://github.com/ruppert/fmini-1, by Ruppert), whose `firmware/src/fx.c` is Felucca's
  (https://github.com/hugelton/Felucca, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments) with
  FMini-1's changes; its small DSP helpers are Felucca-Engines' (`dsp.c`, MIT). `app/fx/fmini_fx.c` says what was
  changed to fit SQ-80.
- **The arpeggiator, sequencer and recording** (`app/seq/`): GPL-3.0-only. They are SLOOP's
  (https://github.com/isod89/sloop-fm1, `firmware/src/seq.c` and `core.h`, Copyright (C) 2026 Leo Kuroshita
  (@kurogedelic), Hügelton Instruments; its MIDI clock after Felucca 1.0's, from contributions by ChanceTheMaker and
  keremimo), ported for one part; `app/seq/sloop_seq.c` says what was kept and what was changed.
- **The built-in waves** (`tools/gen_waves.py`, the `app/waves/sq80_waves.h` it makes): MIT, our own. They are
  synthesized from scratch; only the SQ-80's wave names and slot numbers are followed, so programs find a wave of
  the right kind. No Ensoniq ROM data went into them; programs played with a ROM pack were only compared by
  overall loudness, to set the sampled-instrument waves' level.
- **The web pages' cyan lettering** (`app/site/fonts/DSEG14Classic-Regular.woff2`): DSEG14 Classic, © 2020
  keshikan (https://github.com/keshikan/DSEG, Reserved Font Name "DSEG"), unmodified, under the SIL Open Font License
  1.1 (`app/site/fonts/OFL-DSEG.txt`, also served beside it as `fonts/OFL-DSEG.txt`).
- **The program name's letters on the FM-1** (`app/fonts/seg_font.h`, made by `tools/gen_seg_font.py` from
  `app/fonts/DSEG14Classic-Regular.ttf`): a bitmap rendering of DSEG14 Classic, © 2020 keshikan, under the SIL Open
  Font License 1.1 (`app/fonts/OFL-DSEG.txt`); not named DSEG (its Reserved Font Name). The generator is MIT.
- **fm1-base** (`fm1/base`, a submodule): MPL-2.0, its own terms (its `LICENSING.md`).

**The FM-1 firmware as a whole** (what `app/` builds: the engine, the app, the effects, the sequencer and fm1-base together) is
therefore **GPL-3.0-only**. If you share the firmware, share its complete source with it under the same licence
(this repository, with its submodule, is that source).

No Ensoniq ROM data or programs are in this repository or in the firmware: you load your own.
