/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file diag.h
 * Visual Diagnostics Subsystem for SMK-37 Pro (ST7789 240x240 LCD)
 *
 * Screen Layout & Geometry:
 * - 240x240 pixel screen divided into 16x16 grid of 15x15 pixel squares:
 *     - Col index: 0 .. 15 (X = col * 15)
 *     - Row index: 0 .. 15 (Y = row * 15)
 * - Each square consists of:
 *     - Outer 15x15 square of background mid-grey (1px margin around the outline)
 *     - 13x13 white square outline at (X+1, Y+1)
 *     - 11x11 filled core at (X+2, Y+2) of the patch colour (initially mid-grey)
 *
 * Rows:
 * - Row 0: 24-bit Build Stamp (8 flags representing 3 bits each, values 0..7)
 * - Row 1: Numeric output & Boot mode
 *     - Col 0: Boot Mode (RED: SRAM, BLUE: Flash XIP cold, GREEN: Flash XIP reboot)
 *     - Cols 1..15: Reserved for numeric diagnostics (3-bit numbers 0..7)
 * - Row 2: Early Boot & Core Hardware Progress Flags (RYGCBM rotation)
 * - Row 3: Application & GUI Bringup Progress Flags (RYGCBM rotation)
 *
 * 3-Bit Colour Encoding:
 *   Bit 2 = Red, Bit 1 = Green, Bit 0 = Blue
 *   0 (000): Black (K)
 *   1 (001): Blue (B)
 *   2 (010): Green (G)
 *   3 (011): Cyan (C)
 *   4 (100): Red (R)
 *   5 (101): Magenta (M)
 *   6 (110): Yellow (Y)
 *   7 (111): White (W)
 *
 * Sequential Progress Flags (RYGCBM sequence):
 *   Step 0: Red (R)
 *   Step 1: Yellow (Y)
 *   Step 2: Green (G)
 *   Step 3: Cyan (C)
 *   Step 4: Blue (B)
 *   Step 5: Magenta (M)
 *   (repeats every 6 steps)
 */

#ifndef DIAG_H
#define DIAG_H

#include <stdint.h>

#define DIAG_GRID_COLS       16
#define DIAG_GRID_ROWS       16
#define DIAG_SQUARE_SIZE     15
#define DIAG_OUTLINE_SIZE    13
#define DIAG_CORE_SIZE       11

/* Native ST7789 Byte-Swapped RGB565 Colors (Big-Endian SPI) */
#define DIAG_COLOR_BLACK     0x0000
#define DIAG_COLOR_WHITE     0xFFFF
#define DIAG_COLOR_GREY      0x1084 /* Mid-Grey: RGB(128, 128, 128) */

/* RYGCBM 6-Color Sequence */
#define DIAG_COLOR_RED       0x00F8 /* RGB(255, 0, 0) */
#define DIAG_COLOR_YELLOW    0xE0FF /* RGB(255, 255, 0) */
#define DIAG_COLOR_GREEN     0xE007 /* RGB(0, 255, 0) */
#define DIAG_COLOR_CYAN      0xFF07 /* RGB(0, 255, 255) */
#define DIAG_COLOR_BLUE      0x1F00 /* RGB(0, 0, 255) */
#define DIAG_COLOR_MAGENTA   0x1FF8 /* RGB(255, 0, 255) */

/* Boot Modes for Row 1, Col 0 */
typedef enum {
    DIAG_BOOT_SRAM = 0,         /* RED: Running directly from SRAM (0x01C02000) */
    DIAG_BOOT_FLASH_COLD,       /* BLUE: Running from Flash XIP after cold power cycle */
    DIAG_BOOT_FLASH_REBOOT,     /* GREEN: Running from Flash XIP after initial flash tool reboot */
} DiagBootMode;

/* Predefined Progress Checkpoint Flags */
typedef enum {
    /* =====================================================================
     * Row 2: Core 0 Early Boot & Hardware Setup (Flags 0..15)
     * ===================================================================== */
    DIAG_FLAG_MAIN_ENTRY = 0,         /* Row 2, Col 0  (Red):     main() entry point reached */
    DIAG_FLAG_WDT_DISABLED,           /* Row 2, Col 1  (Yellow):  Watchdog timer closed */
    DIAG_FLAG_INT_INIT_DONE,          /* Row 2, Col 2  (Green):   Core 0 interrupt controller initialized */
    DIAG_FLAG_CLOCK_320M_DONE,        /* Row 2, Col 3  (Cyan):    System clock switched to target clock in SRAM */
#define DIAG_FLAG_CLOCK_INIT_DONE DIAG_FLAG_CLOCK_320M_DONE
    DIAG_FLAG_TIME_INIT_DONE,         /* Row 2, Col 4  (Blue):    Hardware 24MHz Timer1 time base initialized */
    DIAG_FLAG_ST7789_INIT_DONE,       /* Row 2, Col 5  (Magenta): ST7789 LCD hardware initialized & cleared */
    DIAG_FLAG_INPUT_PRE_INIT,         /* Row 2, Col 6  (Red):     Early input listeners registered */
    DIAG_FLAG_MCU_INPUT_INIT_DONE,    /* Row 2, Col 7  (Yellow):  Keybed & ADC scanner task created */
    DIAG_FLAG_GUI_TASK_CREATED,       /* Row 2, Col 8  (Green):   vGuiTask FreeRTOS task created */
    DIAG_FLAG_CORE1_VEC_FLUSHED,      /* Row 2, Col 9  (Cyan):    Core 1 reset vector populated & dcache flushed */
    DIAG_FLAG_SCHEDULER_START,        /* Row 2, Col 10 (Blue):    vTaskStartScheduler() called on Core 0 */

    /* Core 1 Secondary CPU Boot */
    DIAG_FLAG_CORE1_ENTRY,            /* Row 2, Col 11 (Magenta): cpu1_entry() entered on Core 1 */
    DIAG_FLAG_CORE1_INT_INIT,         /* Row 2, Col 12 (Red):     interrupt_init() called on Core 1 */
    DIAG_FLAG_CORE1_SCHEDULER_START,  /* Row 2, Col 13 (Yellow):  vTaskStartScheduler() called on Core 1 */

    /* Spare slots in Row 2 (used as app_init sub-step markers) */
    DIAG_FLAG_SPARE_14,               /* Row 2, Col 14 (Green):   hal_audio_init returned (YELLOW = entered) */
    DIAG_FLAG_SPARE_15,               /* Row 2, Col 15 (Green):   ui_sample_init returned (CYAN = entered) */

    /* =====================================================================
     * Row 3: vGuiTask & Application Bringup (Flags 16..31)
     * ===================================================================== */
    DIAG_FLAG_GUI_TASK_ENTRY,         /* Row 3, Col 0  (Blue):    vGuiTask thread scheduled & executing */
    DIAG_FLAG_LVGL_INIT_DONE,         /* Row 3, Col 1  (Magenta): lv_init() completed */
    DIAG_FLAG_HAL_DISP_INIT_DONE,     /* Row 3, Col 2  (Red):     hal_display_init() completed */
    DIAG_FLAG_APP_INIT_ENTRY,         /* Row 3, Col 3  (Yellow):  app_init() entered */
    DIAG_FLAG_SYNTH_INIT_DONE,        /* Row 3, Col 4  (Green):   synth_init() completed */
    DIAG_FLAG_INPUT_POST_INIT,        /* Row 3, Col 5  (Cyan):    app_register_input() completed */
    DIAG_FLAG_HAL_AUDIO_INIT_DONE,    /* Row 3, Col 6  (Blue):    hal_audio_init() completed */
    DIAG_FLAG_UI_SAMPLE_INIT_DONE,    /* Row 3, Col 7  (Magenta): ui_sample_init() completed */
    DIAG_FLAG_HAL_AUDIO_START_DONE,   /* Row 3, Col 8  (Red):     hal_audio_start() completed */
    DIAG_FLAG_APP_INIT_DONE,          /* Row 3, Col 9  (Yellow):  app_init() returned cleanly */
    DIAG_FLAG_GUI_LOOP_FIRST_TICK,    /* Row 3, Col 10 (Green):   vGuiTask main loop first iteration */

    DIAG_FLAG_COUNT
} DiagFlag;

#if defined(STARTUP_DIAGNOSTICS) && STARTUP_DIAGNOSTICS
/* Core Diagnostics API */
void diag_init(void);
void diag_show_boot_mode(DiagBootMode mode);
void diag_detect_and_show_boot_mode(void);
void diag_set_flag(DiagFlag flag);
void diag_set_square(uint8_t row, uint8_t col, uint16_t color);
void diag_set_number_3bit(uint8_t row, uint8_t col, uint8_t val_0_to_7);
void diag_set_row_numeric(uint8_t row, uint8_t start_col, uint32_t val, uint8_t num_digits_3bit);
uint16_t diag_rygcbm_color(uint8_t step);
uint16_t diag_3bit_color(uint8_t val_0_to_7);
#else
static inline void diag_init(void) {}
static inline void diag_show_boot_mode(DiagBootMode mode) { (void)mode; }
static inline void diag_detect_and_show_boot_mode(void) {}
static inline void diag_set_flag(DiagFlag flag) { (void)flag; }
static inline void diag_set_square(uint8_t row, uint8_t col, uint16_t color) { (void)row; (void)col; (void)color; }
static inline void diag_set_number_3bit(uint8_t row, uint8_t col, uint8_t val_0_to_7) { (void)row; (void)col; (void)val_0_to_7; }
static inline void diag_set_row_numeric(uint8_t row, uint8_t start_col, uint32_t val, uint8_t num_digits_3bit) { (void)row; (void)start_col; (void)val; (void)num_digits_3bit; }
static inline uint16_t diag_rygcbm_color(uint8_t step) { (void)step; return 0; }
static inline uint16_t diag_3bit_color(uint8_t val_0_to_7) { (void)val_0_to_7; return 0; }
#endif

#endif /* DIAG_H */
