/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio): the transport's status, register hook
 * SMK-37 wiring: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) (fm1-fomni, smk37); in SQ-80's copy of fm1-base:
 * Leonardo Lopez Chavira */
/* The M-Vave SMK-37's LCD wiring, in place of boards/fm1/fm1_lcd_hw.h (the SMK build puts boards/smk37 first on the
 * include path), with the same calls: SPI1 group A (IOMAP_CON1 bit 4 = 0) on PB6 CLK, PB7 DO; PC8 CS, PB5 D/C; no
 * backlight control (always on). Group B's data pin, PC10, carries the 16 RGB pad LEDs (WS2812 class) instead:
 * fm1_padled_send switches SPI1 over for one frame while the LCD's CS is high, as the stock firmware does. PC9 (group
 * B's clock) is never touched: it is an analog input on the SMK-37 (mux B of the pads).
 *
 *   fm1_lcd_hw_init()          pins, SPI1 master at BAUD 4
 *   fm1_lcd_baud(b)            SPI1 clock = lsb / (b + 1)
 *   fm1_lcd_send_cmd(c)        D/C low, CS low, one byte (then fm1_lcd_wait)
 *   fm1_lcd_send_data(p, n)    D/C high, CS low, DMA n bytes from RAM p
 *   fm1_lcd_wait()             0 done, -1 timeout (counted); pending cleared, the SPI left on either way
 *   fm1_lcd_spi_off()          SPI1 off
 *   fm1_lcd_deselect()         CS high
 *   fm1_lcd_backlight(on)      nothing (no backlight line on the SMK-37)
 *   fm1_padled_send(p, n)      the LCD deselected and idle: SPI1 to PC10 at BAUD 8, DMA n bytes (then fm1_lcd_wait,
 *                              then fm1_padled_done)
 *   fm1_padled_done()          SPI1 back to the LCD's pins at the LCD's baud */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

#define FM1_LCD_SMK37 1
#ifndef FM1_LCD_REG
#define FM1_LCD_REG(addr) (*(volatile uint32_t *)(addr))
#endif
#define FM1_LCD_PB_OUT FM1_LCD_REG(0x50040u)
#define FM1_LCD_PB_DIR FM1_LCD_REG(0x50048u)
#define FM1_LCD_PC_OUT FM1_LCD_REG(0x50080u)
#define FM1_LCD_PC_DIR FM1_LCD_REG(0x50088u)
#define FM1_LCD_IOMAP_CON1 FM1_LCD_REG(0x51020u)
#define FM1_LCD_SPI_CON FM1_LCD_REG(0x11D00u)
#define FM1_LCD_SPI_BAUD FM1_LCD_REG(0x11D04u)
#define FM1_LCD_SPI_BUF FM1_LCD_REG(0x11D08u)
#define FM1_LCD_SPI_ADR FM1_LCD_REG(0x11D0Cu)
#define FM1_LCD_SPI_CNT FM1_LCD_REG(0x11D10u)
#define FM1_LCD_GROUP_B 0x10u             /* IOMAP_CON1 bit 4: SPI1 on PC8 / PC9 / PC10 */
#define FM1_LCD_CS (1u << 8)              /* PC8 */
#define FM1_LCD_DC (1u << 5)              /* PB5 */
#define FM1_LCD_CLK (1u << 6)             /* PB6 */
#define FM1_LCD_DO (1u << 7)              /* PB7 */
#define FM1_PADLED_DO (1u << 10)          /* PC10 */
#define FM1_PADLED_BAUD 8u                /* lsb / 9 = 6.67 MHz: one SPI byte per LED bit */

static uint32_t fm1_lcd_timeouts;
static uint8_t fm1_lcd_cur_baud = 4u;     /* (the pad LED frames run at their own baud, then this one again) */

FM1_INLINE void fm1_lcd_hw_init(void)
{
    FM1_LCD_IOMAP_CON1 &= ~FM1_LCD_GROUP_B;
    FM1_LCD_PC_OUT |= FM1_LCD_CS;
    FM1_LCD_PC_DIR &= ~FM1_LCD_CS;
    FM1_LCD_PB_OUT &= ~(FM1_LCD_DC | FM1_LCD_CLK | FM1_LCD_DO);
    FM1_LCD_PB_DIR &= ~(FM1_LCD_DC | FM1_LCD_CLK | FM1_LCD_DO);
    FM1_LCD_PC_OUT &= ~FM1_PADLED_DO;     /* the LED line idles low (a latch) */
    FM1_LCD_PC_DIR &= ~FM1_PADLED_DO;
    FM1_LCD_SPI_CON = 0x4021u;
    FM1_LCD_SPI_BAUD = 4u;
    fm1_lcd_cur_baud = 4u;
}

FM1_INLINE void fm1_lcd_backlight(uint32_t on) { (void)on; }

FM1_INLINE void fm1_lcd_baud(uint32_t b)
{
    FM1_LCD_SPI_BAUD = b;
    fm1_lcd_cur_baud = (uint8_t)b;
}

static int fm1_lcd_wait(void)
{
    uint32_t n;
    int rc = 0;
    for (n = 0; n < 4000000u && !(FM1_LCD_SPI_CON & 0x8000u); n++)
        ;
    if (n == 4000000u) {
        fm1_lcd_timeouts++;
        rc = -1;
    }
    FM1_LCD_SPI_CON |= 0x4000u;
    return rc;
}

FM1_INLINE void fm1_lcd_spi_off(void) { FM1_LCD_SPI_CON &= ~1u; }

FM1_INLINE void fm1_lcd_deselect(void) { FM1_LCD_PC_OUT |= FM1_LCD_CS; }

FM1_INLINE void fm1_lcd_send_cmd(uint8_t c)
{
    FM1_LCD_PB_OUT &= ~FM1_LCD_DC;
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_BUF = c;
}

FM1_INLINE void fm1_lcd_send_data(const void *p, uint32_t n)
{
    FM1_LCD_PB_OUT |= FM1_LCD_DC;
    FM1_LCD_PC_OUT &= ~FM1_LCD_CS;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_ADR = (uint32_t)(uintptr_t)p;
    FM1_LCD_SPI_CNT = n;
}

FM1_INLINE void fm1_padled_send(const void *p, uint32_t n)
{
    FM1_LCD_PC_OUT |= FM1_LCD_CS;         /* the panel ignores the bus */
    FM1_LCD_SPI_BAUD = FM1_PADLED_BAUD;
    FM1_LCD_IOMAP_CON1 |= FM1_LCD_GROUP_B;
    FM1_LCD_SPI_CON |= 0x4000u;
    FM1_LCD_SPI_ADR = (uint32_t)(uintptr_t)p;
    FM1_LCD_SPI_CNT = n;
}

FM1_INLINE void fm1_padled_done(void)
{
    FM1_LCD_IOMAP_CON1 &= ~FM1_LCD_GROUP_B;
    FM1_LCD_SPI_BAUD = fm1_lcd_cur_baud;
}
