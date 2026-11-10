/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Flash capacity and application-owned write regions. No register accesses.
 * Define FM1_FLASH_WRITE_REGIONS(X) before inclusion, for example:
 *   #define FM1_FLASH_WRITE_REGIONS(X) X(0x97000u, 0xA0000u)
 * Each X(lo, hi) is a half-open region. A request must fit wholly in one.
 * Bounds must be constants: the RAM driver must not fetch a table from XIP.
 * FL_RANGE_OK remains an override for existing firmware and the loader.
 * The legacy Felucca layout below is only the compatibility default. */
#pragma once
#include <stdint.h>
#include "fm1_board.h"                  /* BOARD_SMK37 */

#ifndef FM1_FLASH_SIZE
#define FM1_FLASH_SIZE 0x100000u
#endif
#if FM1_FLASH_SIZE < 0x100000u || FM1_FLASH_SIZE > 0x1000000u
#error "FM1_FLASH_SIZE must be 1..16 MiB (3-byte addressing only)"
#endif
/* Physical capacity does not establish how much the SFC can map. */
#ifndef FM1_FLASH_XIP_SIZE
#define FM1_FLASH_XIP_SIZE 0x100000u
#endif
#ifndef FM1_FLASH_PLAIN_LO
#if BOARD_SMK37
#define FM1_FLASH_PLAIN_LO 0x9D000u     /* SMK-37: the plain window from its VM start */
#else
#define FM1_FLASH_PLAIN_LO 0x93000u
#endif
#endif
#ifndef FM1_FLASH_PLAIN_HI
#define FM1_FLASH_PLAIN_HI FM1_FLASH_XIP_SIZE
#endif
#if FM1_FLASH_XIP_SIZE > FM1_FLASH_SIZE || FM1_FLASH_PLAIN_LO < 0x4000u || \
    FM1_FLASH_PLAIN_LO >= FM1_FLASH_PLAIN_HI || FM1_FLASH_PLAIN_HI > FM1_FLASH_XIP_SIZE
#error "Invalid flash XIP/plain window"
#endif

#define FL_IN(off, n, lo, hi) ((uint32_t)(hi) >= (uint32_t)(lo) && \
    (uint32_t)(off) >= (uint32_t)(lo) && (uint32_t)(off) <= (uint32_t)(hi) && \
    (uint32_t)(n) <= (uint32_t)(hi) - (uint32_t)(off))

#if BOARD_SMK37
/* ---- the M-Vave SMK-37 (fm1_board.h; hardware notes: github.com/genkigenki/fm1-to-smk37, docs/01-hardware.md) ----
 *   0x00000-0x03FFF  head (SPL, boot config)                    NEVER
 *   0x04000-0x9CFFF  app area                                   the update loader only
 *   0x9D000-0xBAFFF  VM: the app's store and data                FL_DATA (PLAT_DATA)
 *   0xBB000-0xBFFFF  VM: the update's staging and its record      FL_OTA (chip/ota.c)
 *   0xC0000-0xC0FFF  BTIF: Bluetooth pairing                     NEVER
 *   0xC1000-0xC1FFF  USRTRIM: the unit's key, pad and wheel calibration ("trimdat")   NEVER (read only)
 *   0xC2000-0xE9FFF  USRFLASH: the app's big data                FL_XDATA1 (PLAT_XDATA1)
 *   0xEA000-0xF3FFF  not used
 *   0xF4000-0xFDFFF  USR: the stock firmware's resources (kept for the way back)   NEVER
 *   0xFE000-0xFEFFF  not used
 *   0xFF000-0xFFFFF  key_mac                                     NEVER
 * The writable windows are an explicit allow-list; the asserts keep each one clear of the protected ranges, and the
 * RAM driver refuses those ranges on its own as well (FL_PROTECTED in FL_WRITE_OK), whatever FL_RANGE_OK a caller
 * (the update loader) supplies. */
#define FL_HEAD_HI   0x00004000u
#define FL_CAL_LO    0x000C0000u
#define FL_CAL_HI    0x000C2000u
#define FL_USRRES_LO 0x000F4000u
#define FL_USRRES_HI 0x000FE000u
#define FL_KEYMAC_LO 0x000FF000u
#define FL_DATA_LO   0x0009D000u
#define FL_DATA_HI   0x000BB000u
#define FL_OTA_LO    0x000BB000u
#define FL_OTA_HI    0x000C0000u
#define FL_XDATA1_LO 0x000C2000u
#define FL_XDATA1_HI 0x000EA000u
#define FL_WIN_SAFE(lo, hi) ((lo) % 0x1000u == 0u && (hi) % 0x1000u == 0u && (lo) < (hi) && (lo) >= FL_HEAD_HI && \
                             (hi) <= FL_KEYMAC_LO && ((hi) <= FL_CAL_LO || (lo) >= FL_CAL_HI) && \
                             ((hi) <= FL_USRRES_LO || (lo) >= FL_USRRES_HI))
_Static_assert(FL_WIN_SAFE(FL_DATA_LO, FL_DATA_HI), "SMK: FL_DATA overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_OTA_LO, FL_OTA_HI), "SMK: FL_OTA overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_XDATA1_LO, FL_XDATA1_HI), "SMK: FL_XDATA1 overlaps a protected range");
_Static_assert(FL_DATA_HI <= FL_OTA_LO && FL_OTA_HI <= FL_CAL_LO, "SMK: windows overlap");
#ifndef SMK_XDATA2
#define SMK_XDATA2 0
#endif
#if SMK_XDATA2   /* the unlisted stretch after USRFLASH (0xEA000 left alone), only by the app's choice (fm1.toml smk_xdata2) */
#define FL_XDATA2_LO 0x000EB000u
#define FL_XDATA2_HI 0x000F4000u
_Static_assert(FL_WIN_SAFE(FL_XDATA2_LO, FL_XDATA2_HI), "SMK: FL_XDATA2 overlaps a protected range");
_Static_assert(FL_XDATA2_LO > FL_XDATA1_HI && FL_XDATA2_HI <= FL_USRRES_LO, "SMK: FL_XDATA2 after USRFLASH, before USR");
#define FL_STORE_OK(off, n) FL_IN(off, n, FL_DATA_LO, FL_DATA_HI)
#define FL_XDATA_OK(off, n) (FL_IN(off, n, FL_XDATA1_LO, FL_XDATA1_HI) || FL_IN(off, n, FL_XDATA2_LO, FL_XDATA2_HI))
#ifndef FL_RANGE_OK
#ifndef FM1_FLASH_WRITE_REGIONS
#define FM1_FLASH_WRITE_REGIONS(X) X(FL_DATA_LO, FL_DATA_HI) X(FL_XDATA1_LO, FL_XDATA1_HI) X(FL_XDATA2_LO, FL_XDATA2_HI) \
    X(FL_OTA_LO, FL_OTA_HI)
#endif
#endif
#else
#define FL_STORE_OK(off, n) FL_IN(off, n, FL_DATA_LO, FL_DATA_HI)
#define FL_XDATA_OK(off, n) FL_IN(off, n, FL_XDATA1_LO, FL_XDATA1_HI)
#endif
#ifndef FL_RANGE_OK
#ifndef FM1_FLASH_WRITE_REGIONS
#define FM1_FLASH_WRITE_REGIONS(X) X(FL_DATA_LO, FL_DATA_HI) X(FL_XDATA1_LO, FL_XDATA1_HI) X(FL_OTA_LO, FL_OTA_HI)
#endif
#define FM1_FLASH_REGION_OK(lo, hi) || FL_IN(off, n, lo, hi)
static inline __attribute__((always_inline)) int fm1_flash_region_ok(uint32_t off, uint32_t n)
{
    (void)off; (void)n;
    return 0 FM1_FLASH_WRITE_REGIONS(FM1_FLASH_REGION_OK);
}
#undef FM1_FLASH_REGION_OK
#define FL_RANGE_OK(off, n) fm1_flash_region_ok(off, n)
#endif
/* [off, off + n) touches a protected range: the RAM driver's second, independent net */
#define FL_HITS(off, n, lo, hi) ((uint32_t)(off) < (hi) && \
                                 ((uint32_t)(off) >= (lo) || (uint32_t)(n) > (lo) - (uint32_t)(off)))
#define FL_PROTECTED(off, n) (FL_HITS(off, n, 0u, FL_HEAD_HI) || FL_HITS(off, n, FL_CAL_LO, FL_CAL_HI) || \
                              FL_HITS(off, n, FL_USRRES_LO, FL_USRRES_HI) || (uint32_t)(off) >= FL_KEYMAC_LO || \
                              (uint32_t)(n) > FL_KEYMAC_LO - (uint32_t)(off))
#else /* the FM-1 */
/* Compatibility names; new applications supply their own regions above. */
#define FL_DATA_LO 0x00097000u
#define FL_DATA_HI 0x000E0000u
#define FL_GLOB_LO 0x000FC000u
#define FL_GLOB_HI 0x000FF000u
#define FL_OTA_LO  0x000E0000u
#define FL_OTA_HI  0x000E5000u
#define FL_AUTO_LO 0x000E5000u
#define FL_AUTO_HI 0x000E7000u
#define FL_XDATA0_LO 0x00093000u
#define FL_XDATA0_HI 0x00097000u
#define FL_XDATA1_LO 0x000E7000u
#define FL_XDATA1_HI 0x000FC000u
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || \
    FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI) || FL_IN(off, n, FL_AUTO_LO, FL_AUTO_HI))
#define FL_XDATA_OK(off, n) (FL_IN(off, n, FL_XDATA0_LO, FL_XDATA0_HI) || \
    FL_IN(off, n, FL_XDATA1_LO, FL_XDATA1_HI))
#ifndef FL_RANGE_OK
#ifndef FM1_FLASH_WRITE_REGIONS
#define FM1_FLASH_WRITE_REGIONS(X) \
    X(FL_DATA_LO, FL_DATA_HI) X(FL_GLOB_LO, FL_GLOB_HI) \
    X(FL_AUTO_LO, FL_AUTO_HI) X(FL_XDATA0_LO, FL_XDATA0_HI) \
    X(FL_XDATA1_LO, FL_XDATA1_HI) X(FL_OTA_LO, FL_OTA_HI)
#endif
#define FM1_FLASH_REGION_OK(lo, hi) || FL_IN(off, n, lo, hi)
static inline __attribute__((always_inline)) int fm1_flash_region_ok(uint32_t off, uint32_t n)
{
    (void)off; (void)n;                         /* an empty region list is read-only */
    return 0 FM1_FLASH_WRITE_REGIONS(FM1_FLASH_REGION_OK);
}
#undef FM1_FLASH_REGION_OK
#define FL_RANGE_OK(off, n) fm1_flash_region_ok(off, n)
#endif
#define FL_PROTECTED(off, n) 0
#endif /* BOARD_SMK37 */

#define FL_READ_OK(off, n) FL_IN(off, n, 0u, FM1_FLASH_SIZE)
#define FL_WRITE_OK(off, n) (FL_READ_OK(off, n) && FL_RANGE_OK(off, n) && !FL_PROTECTED(off, n))
#define FL_PLAIN_OK(off, n) FL_IN(off, n, FM1_FLASH_PLAIN_LO, FM1_FLASH_PLAIN_HI)
#define FL_RAM_OK(p, n) ((uintptr_t)(p) >= 0x01C00000u && \
    (uintptr_t)(p) <= 0x01C80000u && (uint32_t)(n) <= 0x01C80000u - (uintptr_t)(p))

#define FL_ERR_RANGE   (-1)
#define FL_ERR_MODE    (-2)
#define FL_ERR_WEL     (-3)
#define FL_ERR_TIMEOUT (-4)
#define FL_ERR_BUSY    (-5)
#define FL_ERR_BUFFER  (-6)
