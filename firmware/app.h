/* SPDX-License-Identifier: MIT
 * SQ-80 TEST: the sq80 engine on the FM-1, with a small built-in synthetic wave set (no Ensoniq data).
 * It answers two questions on the hardware: does the engine run, and how much of the CPU does it take. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "SQ-80 TEST"
#define APP_SPLASH_RGB RGB(90, 200, 160)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity from the keys (lib/touch.h) */

enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
