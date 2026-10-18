/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file hal_diag.h
 * Hardware Abstraction Layer for Visual Diagnostics
 *
 * Provides safe, portable wrappers for diag_set_flag and diag_set_square
 * across MCU and Native targets, keeping application code free of #ifdefs.
 */

#ifndef HAL_DIAG_H
#define HAL_DIAG_H

#include <stdint.h>

#if defined(TARGET_MCU)
#include "diag.h"

static inline void hal_diag_set_flag(DiagFlag flag)
{
    diag_set_flag(flag);
}

static inline void hal_diag_set_square(uint8_t row, uint8_t col, uint16_t color)
{
    diag_set_square(row, col, color);
}

#else

/* Predefined Color Constants for Native Simulation */
#define DIAG_COLOR_BLACK     0x0000
#define DIAG_COLOR_WHITE     0xFFFF
#define DIAG_COLOR_GREY      0x1084
#define DIAG_COLOR_RED       0x00F8
#define DIAG_COLOR_YELLOW    0xE0FF
#define DIAG_COLOR_GREEN     0xE007
#define DIAG_COLOR_CYAN      0xFF07
#define DIAG_COLOR_BLUE      0x1F00
#define DIAG_COLOR_MAGENTA   0x1FF8

typedef enum {
    DIAG_FLAG_MAIN_ENTRY = 0,
    DIAG_FLAG_WDT_DISABLED,
    DIAG_FLAG_INT_INIT_DONE,
    DIAG_FLAG_CLOCK_320M_DONE,
    DIAG_FLAG_TIME_INIT_DONE,
    DIAG_FLAG_ST7789_INIT_DONE,
    DIAG_FLAG_INPUT_PRE_INIT,
    DIAG_FLAG_MCU_INPUT_INIT_DONE,
    DIAG_FLAG_GUI_TASK_CREATED,
    DIAG_FLAG_CORE1_VEC_FLUSHED,
    DIAG_FLAG_SCHEDULER_START,
    DIAG_FLAG_CORE1_ENTRY,
    DIAG_FLAG_CORE1_INT_INIT,
    DIAG_FLAG_CORE1_SCHEDULER_START,
    DIAG_FLAG_SPARE_14,
    DIAG_FLAG_SPARE_15,
    DIAG_FLAG_GUI_TASK_ENTRY,
    DIAG_FLAG_LVGL_INIT_DONE,
    DIAG_FLAG_HAL_DISP_INIT_DONE,
    DIAG_FLAG_APP_INIT_ENTRY,
    DIAG_FLAG_SYNTH_INIT_DONE,
    DIAG_FLAG_INPUT_POST_INIT,
    DIAG_FLAG_HAL_AUDIO_INIT_DONE,
    DIAG_FLAG_UI_SAMPLE_INIT_DONE,
    DIAG_FLAG_HAL_AUDIO_START_DONE,
    DIAG_FLAG_APP_INIT_DONE,
    DIAG_FLAG_GUI_LOOP_FIRST_TICK,
    DIAG_FLAG_COUNT
} DiagFlag;

static inline void hal_diag_set_flag(DiagFlag flag)
{
    (void)flag;
}

static inline void hal_diag_set_square(uint8_t row, uint8_t col, uint16_t color)
{
    (void)row;
    (void)col;
    (void)color;
}

#endif

#endif /* HAL_DIAG_H */
