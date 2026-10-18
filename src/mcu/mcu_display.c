/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_display.c
 * MCU Display Driver implementation using ST7789 SPI1 DMA for LVGL
 */

#include "hal_display.h"
#include "st7789.h"
#include <stddef.h>

static lv_disp_draw_buf_t disp_draw_buf;
static lv_color_t disp_buf1[LCD_WIDTH * 40] __attribute__((aligned(32)));
static lv_disp_drv_t disp_drv;

static void mcu_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    __asm__ volatile("ssync; csync" ::: "memory");

    st7789_set_window((uint16_t)area->x1, (uint16_t)area->y1, (uint16_t)area->x2, (uint16_t)area->y2);
    st7789_write_pixels((const uint16_t *)color_p, w * h);

    lv_disp_flush_ready(drv);
}

int hal_display_init(void)
{
    /* Initialize LVGL draw buffer */
    lv_disp_draw_buf_init(&disp_draw_buf, disp_buf1, NULL, LCD_WIDTH * 40);

    /* Register LVGL display driver */
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = mcu_disp_flush;
    disp_drv.draw_buf = &disp_draw_buf;
    lv_disp_drv_register(&disp_drv);

    /* Ensure backlight is ON */
    st7789_backlight(1);

    return 0;
}

void hal_display_backlight(uint8_t enable)
{
    st7789_backlight(enable);
}
