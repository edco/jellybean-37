/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_led.c
 * MCU implementation of HAL LED interface for SMK-37 Pro:
 * - Panel button LEDs (19 lamps on 74HC595 shift registers & bank selects)
 * - Drum pad RGB LEDs (16 WS2812B addressable LEDs via JL_SPI1 on PC10)
 * - LCD screen backlight (PB7 via st7789 driver)
 */

#include "hal_led.h"
#include "st7789.h"
#include "asm/WL82.h"
#include "driver/device/gpio.h"
#include <string.h>

/* Low-level lamp interface in mcu_input.c */
extern void mcu_input_set_lamp(uint8_t lamp_idx, bool on);
extern bool mcu_input_get_lamp(uint8_t lamp_idx);
extern void mcu_input_set_lamp_mask(uint32_t mask);
extern uint32_t mcu_input_get_lamp_mask(void);


/* =========================================================================
 * 1. Panel Button LEDs
 * ========================================================================= */
void hal_led_set_button(SmkBtnLed led, bool on)
{
    mcu_input_set_lamp((uint8_t)led, on);
}

bool hal_led_get_button(SmkBtnLed led)
{
    return mcu_input_get_lamp((uint8_t)led);
}

void hal_led_toggle_button(SmkBtnLed led)
{
    bool curr = mcu_input_get_lamp((uint8_t)led);
    mcu_input_set_lamp((uint8_t)led, !curr);
}

void hal_led_set_button_mask(uint32_t mask)
{
    mcu_input_set_lamp_mask(mask);
}

uint32_t hal_led_get_button_mask(void)
{
    return mcu_input_get_lamp_mask();
}

/* =========================================================================
 * 2. Drum Pad RGB LEDs (WS2812B Daisy Chain on PC10)
 *
 * Physical daisy chain snaking table from stock ROM @ 0x0205DD60:
 * Logical Pad Index -> Physical WS2812B Chain Position (0..15).
 * ========================================================================= */
static const uint8_t pad_led_map[SMK_NUM_PAD_LEDS] = {
    0, 15, 14, 13, 12, 11, 10, 9, 1, 2, 3, 4, 5, 6, 7, 8
};

static SmkRgb pad_colors[SMK_NUM_PAD_LEDS] = {{0}};
static bool pad_leds_dirty = false;

/* WS2812B DMA buffer:
 * - Pre-reset pulse: 256 bytes (low time > 307 us) to guarantee complete reset
 *   and wipe any crossbar switch / boot line glitches before first data bit
 * - Data: 384 bytes (16 pads * 24 bytes bit-encoded)
 * - Post-reset latch: 128 bytes (low time > 153 us) for solid latching
 * Total length = 768 bytes */
#define WS2812_PRE_RESET_BYTES   256
#define WS2812_DATA_BYTES        (SMK_NUM_PAD_LEDS * 24)
#define WS2812_POST_RESET_BYTES  128
#define WS2812_FRAME_BYTES       (WS2812_PRE_RESET_BYTES + WS2812_DATA_BYTES + WS2812_POST_RESET_BYTES) /* 768 */

static uint8_t ws2812_dma_buf[WS2812_FRAME_BYTES] __attribute__((aligned(32)));

void hal_led_set_pad_rgb(uint8_t pad_idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (pad_idx >= SMK_NUM_PAD_LEDS) return;
    pad_colors[pad_idx].r = r;
    pad_colors[pad_idx].g = g;
    pad_colors[pad_idx].b = b;
    pad_leds_dirty = true;
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
    pad_leds_dirty = true;
}

void hal_led_update_pads(void)
{
    if (!pad_leds_dirty) return;

    /* 1. Format the 16 RGB values into WS2812B byte-encoded GRB bit stream */
    memset(ws2812_dma_buf, 0, WS2812_FRAME_BYTES);

    for (uint8_t p = 0; p < SMK_NUM_PAD_LEDS; p++) {
        uint8_t phys = pad_led_map[p];
        SmkRgb c = pad_colors[p];

        /* WS2812B GRB format: 24 bits (Green: 23..16, Red: 15..8, Blue: 7..0) */
        uint32_t grb = ((uint32_t)c.g << 16) | ((uint32_t)c.r << 8) | (uint32_t)c.b;

        uint8_t *dest = &ws2812_dma_buf[WS2812_PRE_RESET_BYTES + (phys * 24)];
        for (int8_t bit = 23; bit >= 0; bit--) {
            /* 0xF0 = high pulse for '1' bit, 0xC0 = short pulse for '0' bit */
            *dest++ = (grb & (1u << bit)) ? 0xF0 : 0xC0;
        }
    }

    __asm__ volatile("ssync; csync" ::: "memory");

    /* 2. Configure SPI1 for WS2812B bit transmission:
     * At 40 MHz LSB, BAUD = 5 gives 40 / (5+1) = 6.67 MHz (150 ns/bit):
     * 0xC0 (2 bits high) = 300 ns T0H (nominal WS2812B/SK6812 T0H is 300 ns)
     * 0xF0 (4 bits high) = 600 ns T1H (nominal WS2812B/SK6812 T1H is 600 ns)
     * Total period = 8 * 150 ns = 1.20 us (nominal 1.25 us) */
    JL_SPI1->CON = 0x4021;
    JL_SPI1->BAUD = 0x0005; /* BAUD = 5 -> 6.67 MHz */
    JL_SPI1->ADR = (uint32_t)ws2812_dma_buf;
    JL_SPI1->CNT = WS2812_FRAME_BYTES;

    /* 3. Switch pin crossbar to PC10 while DMA sends the pre-reset zero porch (>300 us) */
    JL_IOMAP->CON1 |= 0x10;

    /* 4. Wait for WS2812B transmission to complete (~920 us) */
    uint32_t timeout = 2000000;
    while ((JL_SPI1->CON & 0x8000) == 0 && --timeout);
    JL_SPI1->CON |= 0x4000;

    /* 5. Ensure PC10 is driven low as GPIO before disconnecting crossbar */
    gpio_direction_output(IO_PORTC_10, 0);

    /* 6. Restore SPI1 configuration for ST7789 LCD */
    JL_SPI1->BAUD = ST7789_SPI_BAUD;

    /* 7. Switch crossbar back to LCD (bit 4 cleared) */
    JL_IOMAP->CON1 &= ~0x10;

    pad_leds_dirty = false;
}

/* =========================================================================
 * 3. LCD Backlight
 * ========================================================================= */
static bool backlight_on = true;

void hal_led_set_backlight(bool on)
{
    backlight_on = on;
    st7789_backlight(on ? 1 : 0);
}

bool hal_led_get_backlight(void)
{
    return backlight_on;
}

/* =========================================================================
 * Subsystem Initialization
 * ========================================================================= */
void hal_led_init(void)
{
    /* 1. Configure Pad 42 (PC10) as GPIO output driven low with pull-down */
    gpio_direction_output(IO_PORTC_10, 0);
    gpio_set_die(IO_PORTC_10, 1);
    gpio_set_pull_up(IO_PORTC_10, 0);
    gpio_set_pull_down(IO_PORTC_10, 1);

    /* 2. Clear all 16 pad LEDs to black and flush twice to ensure solid latch */
    hal_led_set_all_pads(0, 0, 0);
    hal_led_update_pads();
    pad_leds_dirty = true;
    hal_led_update_pads();

    /* 3. Turn on LCD backlight by default */
    hal_led_set_backlight(true);
}
