/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file sim_led.cpp
 * Host simulation backend for HAL LED interface.
 */

#include "hal_led.h"
#include <cstdio>
#include <cstring>

static uint32_t btn_led_mask = 0;
static SmkRgb pad_colors[SMK_NUM_PAD_LEDS] = {{0}};
static bool backlight_on = true;

extern "C" {

void hal_led_init(void)
{
    btn_led_mask = 0;
    std::memset(pad_colors, 0, sizeof(pad_colors));
    backlight_on = true;
}

void hal_led_set_button(SmkBtnLed led, bool on)
{
    if (led >= SMK_LED_BTN_COUNT) return;
    if (on) {
        btn_led_mask |= (1u << led);
    } else {
        btn_led_mask &= ~(1u << led);
    }
}

bool hal_led_get_button(SmkBtnLed led)
{
    if (led >= SMK_LED_BTN_COUNT) return false;
    return (btn_led_mask & (1u << led)) != 0;
}

void hal_led_toggle_button(SmkBtnLed led)
{
    if (led >= SMK_LED_BTN_COUNT) return;
    btn_led_mask ^= (1u << led);
}

void hal_led_set_button_mask(uint32_t mask)
{
    btn_led_mask = mask & 0x7FFFF;
}

uint32_t hal_led_get_button_mask(void)
{
    return btn_led_mask;
}

void hal_led_set_pad_rgb(uint8_t pad_idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (pad_idx >= SMK_NUM_PAD_LEDS) return;
    pad_colors[pad_idx].r = r;
    pad_colors[pad_idx].g = g;
    pad_colors[pad_idx].b = b;
}

void hal_led_set_pad_color(uint8_t pad_idx, uint32_t rgb24)
{
    uint8_t r = (uint8_t)((rgb24 >> 16) & 0xFF);
    uint8_t g = (uint8_t)((rgb24 >> 8) & 0xFF);
    uint8_t b = (uint8_t)(rgb24 & 0xFF);
    hal_led_set_pad_rgb(pad_idx, r, g, b);
}

SmkRgb hal_led_get_pad_rgb(uint8_t pad_idx)
{
    if (pad_idx >= SMK_NUM_PAD_LEDS) {
        SmkRgb zero = {0, 0, 0};
        return zero;
    }
    return pad_colors[pad_idx];
}

void hal_led_set_all_pads(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint8_t i = 0; i < SMK_NUM_PAD_LEDS; i++) {
        pad_colors[i].r = r;
        pad_colors[i].g = g;
        pad_colors[i].b = b;
    }
}

void hal_led_update_pads(void)
{
    /* In native simulator, pad colors are updated in pad_colors in memory */
}

void hal_led_set_backlight(bool on)
{
    backlight_on = on;
}

bool hal_led_get_backlight(void)
{
    return backlight_on;
}

} /* extern "C" */
