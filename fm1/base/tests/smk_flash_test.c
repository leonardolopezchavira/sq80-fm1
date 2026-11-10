/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leonardo Lopez Chavira (SQ-80's copy of fm1-base) */
/* The M-Vave SMK-37's flash rules (bsp/chip/fm1_flash_policy.h with BOARD_SMK37): the driver's gate FL_WRITE_OK takes
 * the app's windows and the update's staging, and refuses the protected ranges, also when a caller (the update loader)
 * supplies a wider FL_RANGE_OK of its own.
 *   cc -I bsp/chip -DBOARD_SMK37=1 tests/smk_flash_test.c && ./a.out
 *   cc -I bsp/chip -DBOARD_SMK37=1 -DLOADER tests/smk_flash_test.c && ./a.out
 * (and each with -DSMK_XDATA2=1: the stretch 0xEB000..0xF3FFF too) */
#include <stdint.h>
#include <stdio.h>
#ifdef LOADER   /* the loader's own, as bsp/loader/loader.c has it: the protected ranges must still be refused */
#define FL_RANGE_OK(off, n) (FL_IN(off, n, 0x4000u, 0x9D000u) || FL_IN(off, n, 0xBB000u, 0xC0000u) || 1)
#endif
#include "fm1_flash_policy.h"

static int bad;
static void want(uint32_t off, uint32_t n, int ok)
{
    if (!!FL_WRITE_OK(off, n) != ok) {
        printf("FAIL %05x + %x: %s\n", off, n, ok ? "refused" : "allowed");
        bad++;
    }
}

int main(void)
{
    uint32_t a;
    for (a = 0; a < 0x100000u; a += 0x1000u) {   /* every sector: never one of the protected ranges */
        int prot = a < 0x4000u || (a >= 0xC0000u && a < 0xC2000u) || (a >= 0xF4000u && a < 0xFE000u) || a >= 0xFF000u;
        if (prot)
            want(a, 0x1000u, 0);
    }
    want(0xBFFFFu, 2, 0);                          /* across into BTIF */
    want(0xC1000u, 1, 0);                          /* the calibration */
    want(0xC1224u, 8, 0);
    want(0xFF000u, 1, 0);
    want(0x3FFFu, 1, 0);
    want(0xFFFFFFFFu, 2, 0);                       /* a wrap */
#ifndef LOADER
    want(0x9D000u, 0x1000u, 1);                    /* the store */
    want(0xBA000u, 0x1000u, 1);
    want(0xBB000u, 0x1000u, 1);                    /* the update's staging */
    want(0xC2000u, 0x1000u, 1);                    /* USRFLASH */
    want(0xE9000u, 0x1000u, 1);
    want(0xEA000u, 0x1000u, 0);                    /* not ours */
#if SMK_XDATA2
    want(0xEB000u, 0x1000u, 1);                    /* the unlisted stretch, chosen (fm1.toml smk_xdata2) */
    want(0xF3000u, 0x1000u, 1);
    want(0xF3FFFu, 2, 0);                          /* across into USR */
#else
    want(0xEB000u, 0x1000u, 0);
    want(0xF3000u, 0x1000u, 0);
#endif
    want(0x4000u, 0x1000u, 0);                     /* the app area: the loader's only */
    want(0x9C000u, 0x1000u, 0);
#endif
    printf(bad ? "smk_flash_test: %d failures\n" : "smk_flash_test: ok\n", bad);
    return bad != 0;
}
