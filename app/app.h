/* SPDX-License-Identifier: MIT
 * SQ-80: the sq80 engine on the FM-1, playing the player's own SQ-80 ROM pack and programs from flash
 * (app.c says how they get there). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "SQ-80"
#define APP_SPLASH_RGB RGB(90, 200, 160)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity from the keys (lib/touch.h) */
#define APP_SYSEX                        /* the loader's frames, and ESQ-1 single-program dumps */
int app_sysex(const uint8_t *p, uint32_t n);

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
