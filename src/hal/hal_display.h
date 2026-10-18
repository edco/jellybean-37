/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file hal_display.h
 * Hardware Abstraction Layer for Display Subsystem (ST7789 240x240 RGB565)
 */

#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_WIDTH   240
#define LCD_HEIGHT  240

/**
 * Initialize display controller and register with LVGL.
 * Sets up hardware display driver, draw buffers, and flush callbacks.
 * @return 0 on success, negative error code on failure.
 */
int hal_display_init(void);

/**
 * Enable or disable display backlight (0 = off, 1 = on).
 */
void hal_display_backlight(uint8_t enable);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISPLAY_H */
