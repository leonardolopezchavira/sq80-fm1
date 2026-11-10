/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leonardo Lopez Chavira (SQ-80); after the SMK-37 layer of M. Geisler (genkigenki/makkuro),
 * fm1-to-smk37 and fm1-fomni's smk37 branch (GPL-3.0) */
/* The board a build is for: BOARD_SMK37 0 = the M-VAVE FM-1 (the default: nothing changes for it), 1 = the M-Vave
 * SMK-37 Pro (and the Donner Starrykey 37 Play, the same keyboard), the SMK-37 Elite or the MKE-P37: the same AC791N,
 * second-stage bootloader and update loader as the FM-1, another panel and flash map. tools/build.py sets both from
 * BOARD=smk37 and MODEL=pro|elite|mke. SMK_MODEL: the model name the build reports (its identity and its update
 * loader's): M-Vave's updater takes back only a unit's own factory package. */
#pragma once
#ifndef BOARD_SMK37
#define BOARD_SMK37 0
#endif
#if BOARD_SMK37 && !defined(SMK_MODEL)
#define SMK_MODEL "SMK-37 Pro"
#endif
