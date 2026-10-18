/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file diag.c
 * Visual Diagnostics Implementation for SMK-37 Pro (ST7789 240x240 LCD)
 */

#include "diag.h"
#include "st7789.h"
#include "build_id.h"
#include "hal_time.h"

extern uint8_t p33_rx_1byte(uint16_t addr);
extern int main(void);

static const uint16_t RYGCBM_TABLE[6] = {
    DIAG_COLOR_RED,     /* 0: Red */
    DIAG_COLOR_YELLOW,  /* 1: Yellow */
    DIAG_COLOR_GREEN,   /* 2: Green */
    DIAG_COLOR_CYAN,    /* 3: Cyan */
    DIAG_COLOR_BLUE,    /* 4: Blue */
    DIAG_COLOR_MAGENTA  /* 5: Magenta */
};

uint16_t diag_rygcbm_color(uint8_t step)
{
    return RYGCBM_TABLE[step % 6];
}

uint16_t diag_3bit_color(uint8_t val_0_to_7)
{
    /* 3-bit RGB: Bit 2 = Red, Bit 1 = Green, Bit 0 = Blue */
    uint16_t r = (val_0_to_7 & 4) ? 0x1F : 0;
    uint16_t g = (val_0_to_7 & 2) ? 0x3F : 0;
    uint16_t b = (val_0_to_7 & 1) ? 0x1F : 0;

    /* RGB565 little-endian in memory -> byte-swapped for ST7789 big-endian SPI */
    uint16_t c = (r << 11) | (g << 5) | b;
    return (uint16_t)((c << 8) | (c >> 8));
}

void diag_set_square(uint8_t row, uint8_t col, uint16_t color)
{
    if (row >= DIAG_GRID_ROWS || col >= DIAG_GRID_COLS) {
        return;
    }

    uint16_t x = (uint16_t)col * DIAG_SQUARE_SIZE;
    uint16_t y = (uint16_t)row * DIAG_SQUARE_SIZE;

    /* Fill the 11x11 interior core */
    st7789_fill_rect(x + 2, y + 2, DIAG_CORE_SIZE, DIAG_CORE_SIZE, color);
}

static void draw_flag_outline(uint8_t row, uint8_t col)
{
    uint16_t x = (uint16_t)col * DIAG_SQUARE_SIZE;
    uint16_t y = (uint16_t)row * DIAG_SQUARE_SIZE;

    /* 13x13 white border */
    st7789_fill_rect(x + 1, y + 1, DIAG_OUTLINE_SIZE, DIAG_OUTLINE_SIZE, DIAG_COLOR_WHITE);
    /* 11x11 interior reset to background mid-grey */
    st7789_fill_rect(x + 2, y + 2, DIAG_CORE_SIZE, DIAG_CORE_SIZE, DIAG_COLOR_GREY);
}

void diag_set_flag(DiagFlag flag)
{
    if (flag >= DIAG_FLAG_COUNT || flag == DIAG_FLAG_SPARE_14 || flag == DIAG_FLAG_SPARE_15) {
        return;
    }

    /* Place sequential progress flags in Rows 2 & 3 per diag.h layout */
    uint8_t row = 2 + ((uint8_t)flag / DIAG_GRID_COLS);
    uint8_t col = (uint8_t)flag % DIAG_GRID_COLS;
    uint16_t color = diag_rygcbm_color((uint8_t)flag);

    diag_set_square(row, col, color);
}

void diag_set_number_3bit(uint8_t row, uint8_t col, uint8_t val_0_to_7)
{
    diag_set_square(row, col, diag_3bit_color(val_0_to_7 & 0x7));
}

void diag_set_row_numeric(uint8_t row, uint8_t start_col, uint32_t val, uint8_t num_digits_3bit)
{
    for (int i = 0; i < num_digits_3bit; i++) {
        uint8_t col = start_col + i;
        if (col >= DIAG_GRID_COLS) break;
        uint8_t shift = (num_digits_3bit - 1 - i) * 3;
        uint8_t chunk = (val >> shift) & 0x7;
        diag_set_number_3bit(row, col, chunk);
    }
}

void diag_show_boot_mode(DiagBootMode mode)
{
    uint16_t color;
    switch (mode) {
    case DIAG_BOOT_SRAM:
        color = DIAG_COLOR_RED;
        break;
    case DIAG_BOOT_FLASH_COLD:
        color = DIAG_COLOR_BLUE;
        break;
    case DIAG_BOOT_FLASH_REBOOT:
        color = DIAG_COLOR_GREEN;
        break;
    default:
        color = DIAG_COLOR_GREY;
        break;
    }
    diag_set_square(1, 0, color);
}

void diag_detect_and_show_boot_mode(void)
{
    /* If entry point is in SRAM (< 0x02000000), running directly from SRAM */
    if ((uint32_t)main < 0x02000000) {
        diag_show_boot_mode(DIAG_BOOT_SRAM);
    } else {
        /* Running from Flash: use indirect call across 23-bit address boundary to RAM */
        uint8_t (*volatile p33_rx_fn)(uint16_t) = p33_rx_1byte;
        uint8_t p33_rst = p33_rx_fn(0x00);
        if (p33_rst & 0x01) {
            diag_show_boot_mode(DIAG_BOOT_FLASH_REBOOT);
        } else {
            diag_show_boot_mode(DIAG_BOOT_FLASH_COLD);
        }
    }
}

void diag_init(void)
{
    /* 1. Paint entire screen to background mid-grey */
    st7789_fill_screen(DIAG_COLOR_GREY);

    /* 2. Row 0: 24-bit Build Stamp (8 flags: cols 0..7) */
    for (uint8_t c = 0; c < 8; c++) {
        draw_flag_outline(0, c);
    }
    diag_set_row_numeric(0, 0, build_id_get_24bit(), 8);

    /* 3. Row 1: Boot Mode (1 flag: col 0) */
    draw_flag_outline(1, 0);
    diag_detect_and_show_boot_mode();

    /* 4. Row 2: Early Boot & Core Hardware (16 flags: cols 0..15) */
    for (uint8_t c = 0; c < 16; c++) {
        draw_flag_outline(2, c);
    }

    /* 5. Row 3: Application & GUI Bringup (11 flags: cols 0..10) */
    for (uint8_t c = 0; c < 11; c++) {
        draw_flag_outline(3, c);
    }

    /* 6. Row 4: UI Sample Init Micro-steps (16 flags: cols 0..15) */
    for (uint8_t c = 0; c < 16; c++) {
        draw_flag_outline(4, c);
    }

    /* 7. Row 5: Subsystem Replacement Self-Test (8 flags: cols 0..7) */
    for (uint8_t c = 0; c < 8; c++) {
        draw_flag_outline(5, c);
    }
}
