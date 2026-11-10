/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M. Geisler (genkigenki/makkuro) (fm1-fomni, branch smk37: smk_input.h's fm1_adc.h stand-ins)
 * In SQ-80's copy of fm1-base: Leonardo Lopez Chavira */
/* The M-Vave SMK-37's stand-in for boards/fm1/fm1_adc.h: the scan (fm1_input.h, its mux C) reads the battery; the
 * FM-1's MASTER pot is knob K1 here, as a virtual pot (0..1023, 24 a detent; it starts at 724, the FM-1's middle of
 * the gain). fm1_an keeps device/main_fm1.c's fields: the scan never owns a converter the main loop would read. */
#pragma once
#include <stdint.h>
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
#define SMK_ENC_MASTER 0u          /* K1, where the FM-1 has its MASTER pot (top left) */
static struct { uint16_t con, pend, master, batt; } fm1_an;
static int32_t smk_master = 724;
static void fm1_adc_init(void) {}
static int32_t fm1_adc_read(uint32_t ch)
{
    if (ch == FM1_ADC_BATT)
        return fm1_in.batt ? (int32_t)fm1_in.batt : -1;
    if (ch == FM1_ADC_MASTER) {
        smk_master += fm1_enc_take(SMK_ENC_MASTER) * 24;
        smk_master = smk_master < 0 ? 0 : smk_master > 1023 ? 1023 : smk_master;
        return smk_master;
    }
    return -1;
}
