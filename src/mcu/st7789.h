/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file st7789.h
 * ST7789 240x240 RGB565 LCD Hardware Driver for JieLi AC791N (SMK-37 Pro)
 */

#ifndef ST7789_H
#define ST7789_H

#include <stdint.h>
#include <stddef.h>

#define LCD_WIDTH   240
#define LCD_HEIGHT  240

/* SPI1 Baud rate divider for ST7789:
 * SPI_CLK = LSB_CLK / (ST7789_SPI_BAUD + 1)
 *   0 -> 40 MHz @ 320M sys_clk (45 MHz @ 360M)
 *   1 -> 20 MHz @ 320M sys_clk
 *   4 ->  8 MHz @ 320M sys_clk
 */
#define ST7789_SPI_BAUD         0

/* Standard RGB565 Color Definitions */
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_RED         0xF800
#define COLOR_GREEN       0x07E0
#define COLOR_BLUE        0x001F
#define COLOR_YELLOW      0xFFE0
#define COLOR_CYAN        0x07FF
#define COLOR_MAGENTA     0xF81F
#define COLOR_ORANGE      0xFD20
#define COLOR_PURPLE      0x780F
#define COLOR_DARK_GREY   0x2104
#define COLOR_LIGHT_GREY  0x8410

/* Convert 8-bit RGB to 16-bit RGB565 with byte-swapping for ST7789 */
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    /* Byte-swap for big-endian SPI transmission */
    return (uint16_t)((c << 8) | (c >> 8));
}

/* Driver API */
void st7789_init(void);
void st7789_backlight(uint8_t enable);
void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void st7789_write_pixels(const uint16_t *pixels, uint32_t count);
void st7789_fill_screen(uint16_t color);
void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void st7789_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void st7789_flush_framebuffer(const uint16_t *framebuffer);

#endif /* ST7789_H */
