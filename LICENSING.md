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
- **fm1-base** (`fm1/base`, a submodule): MPL-2.0, its own terms (its `LICENSING.md`).

**The FM-1 firmware as a whole** (what `app/` builds: the engine, the app, the effects, the sequencer and fm1-base together) is
therefore **GPL-3.0-only**. If you share the firmware, share its complete source with it under the same licence
(this repository, with its submodule, is that source).

No Ensoniq ROM data or programs are in this repository or in the firmware: you load your own.
