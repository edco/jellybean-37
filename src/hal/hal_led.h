/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file hal_led.h
 * Hardware Abstraction Layer for SMK-37 Pro Board LEDs:
 * - 19 Front-panel button LEDs (multiplexed across 2 banks via 74HC595 shift registers)
 * - 16 Drum pad RGB LEDs (WS2812B addressable protocol via JL_IMD controller on PC10)
 * - LCD screen backlight (PB7 digital output)
 * - Battery charge indicators (hardware-driven circuit documentation)
 */

#ifndef HAL_LED_H
#define HAL_LED_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 1. Panel Button LEDs
 *
 * Mapped to the stock 19-lamp shift-register multiplexer @ 0x02057CBC.
 * Driven via the four 74HC595 shift registers (bytes 0 & 1) and two
 * bank-select lines on JL_SPECIAL_PAD_CFG (0x00016A0C bits 17/18).
 * ========================================================================= */
typedef enum {
    SMK_LED_BTN_KNOB_BANK = 0,   /* Lamp  0 (White): Knob Bank button */
    SMK_LED_BTN_FADER_BANK,      /* Lamp  1 (White): Fader Bank button */
    SMK_LED_BTN_NAV_LEFT,        /* Lamp  2 (White): Nav Left button */
    SMK_LED_BTN_NAV_RIGHT,       /* Lamp  3 (White): Nav Right button */
    SMK_LED_BTN_OCTAVE_DOWN,     /* Lamp  4 (White): Octave Down button */
    SMK_LED_BTN_OCTAVE_UP,       /* Lamp  5 (White): Octave Up button */
    SMK_LED_BTN_ARP,             /* Lamp  6 (White): Arp button */
    SMK_LED_BTN_NOTE_REPEAT,     /* Lamp  7 (White): Note Repeat button */
    SMK_LED_BTN_SCALE,           /* Lamp  8 (White): Scale button */
    SMK_LED_BTN_CHORD,           /* Lamp  9 (White): Chord button */
    SMK_LED_BTN_GLOBE,           /* Lamp 10 (White): Globe button */
    SMK_LED_BTN_BT,              /* Lamp 11 (Blue):  Bluetooth button */
    SMK_LED_BTN_PATCH,           /* Lamp 12 (White): Patch button */
    SMK_LED_BTN_PARA,            /* Lamp 13 (White): Para button */
    SMK_LED_BTN_FX,              /* Lamp 14 (White): FX button */
    SMK_LED_BTN_SEQ,             /* Lamp 15 (White): Seq button */
    SMK_LED_BTN_SEQ_PLAY_GREEN,  /* Lamp 16 (Green): Seq Play button green element */
    SMK_LED_BTN_SEQ_PLAY_RED,    /* Lamp 17 (Red):   Seq Play button red element */
    SMK_LED_BTN_SEQ_REC,         /* Lamp 18 (Red):   Seq Rec button red element */
    SMK_LED_BTN_COUNT = 19
} SmkBtnLed;

/* Alias for single-color reference to Seq Play */
#define SMK_LED_BTN_SEQ_PLAY     SMK_LED_BTN_SEQ_PLAY_GREEN

/**
 * Initialize LED subsystem.
 */
void hal_led_init(void);

/**
 * Turn a panel button LED on or off.
 * @param led   Button LED identifier (0..18).
 * @param on    true to illuminate, false to extinguish.
 */
void hal_led_set_button(SmkBtnLed led, bool on);

/**
 * Read the current requested state of a button LED.
 * @param led   Button LED identifier (0..18).
 * @return      true if LED is illuminated, false if off.
 */
bool hal_led_get_button(SmkBtnLed led);

/**
 * Toggle a panel button LED state.
 * @param led   Button LED identifier (0..18).
 */
void hal_led_toggle_button(SmkBtnLed led);

/**
 * Set the state of all 19 button LEDs at once via bitmask.
 * Bit i (0..18) corresponds to SMK_LED_BTN_* index.
 * @param mask  Bitmask of desired LED states (1 = ON, 0 = OFF).
 */
void hal_led_set_button_mask(uint32_t mask);

/**
 * Get the current bitmask of all 19 button LEDs.
 * @return      Bitmask of LED states (1 = ON, 0 = OFF).
 */
uint32_t hal_led_get_button_mask(void);


/* =========================================================================
 * 2. Drum Pad RGB LEDs (16 Silicone Pads)
 *
 * Driven as a WS2812B addressable daisy chain on Pad 0x2A (PC10) via the
 * JieLi JL_IMD hardware DMA engine with 24-bit GRB encoding.
 * ========================================================================= */
#define SMK_NUM_PAD_LEDS 16

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} SmkRgb;

/**
 * Set the RGB color of a specific drum pad (0..15).
 * Changes are staged locally and committed to hardware on hal_led_update_pads().
 * @param pad_idx   Drum pad index (0..15).
 * @param r         Red brightness (0..255).
 * @param g         Green brightness (0..255).
 * @param b         Blue brightness (0..255).
 */
void hal_led_set_pad_rgb(uint8_t pad_idx, uint8_t r, uint8_t g, uint8_t b);

/**
 * Set the 24-bit RGB color (0x00RRGGBB) of a specific drum pad (0..15).
 * @param pad_idx   Drum pad index (0..15).
 * @param rgb24     24-bit RGB value (0xRRGGBB).
 */
void hal_led_set_pad_color(uint8_t pad_idx, uint32_t rgb24);

/**
 * Get the current staged RGB color of a drum pad.
 * @param pad_idx   Drum pad index (0..15).
 * @return          SmkRgb structure with current R, G, B values.
 */
SmkRgb hal_led_get_pad_rgb(uint8_t pad_idx);

/**
 * Set all 16 drum pad RGB LEDs to the same color.
 * @param r         Red brightness (0..255).
 * @param g         Green brightness (0..255).
 * @param b         Blue brightness (0..255).
 */
void hal_led_set_all_pads(uint8_t r, uint8_t g, uint8_t b);

/**
 * Push staged pad RGB colors to physical WS2812B LEDs via IMD DMA.
 * Safe to call from UI/application loop.
 */
void hal_led_update_pads(void);


/* =========================================================================
 * 3. LCD Screen Backlight
 *
 * Dedicated digital output pin PB7 (IO_PORTB_07).
 * ========================================================================= */
/**
 * Enable or disable LCD screen backlight.
 * @param on    true = backlight ON, false = backlight OFF.
 */
void hal_led_set_backlight(bool on);

/**
 * Get LCD screen backlight state.
 * @return      true if backlight is ON, false if OFF.
 */
bool hal_led_get_backlight(void);


/* =========================================================================
 * 4. Battery Charge Indicators
 *
 * Hardware Note:
 * On the SMK-37 Pro, the red charging LED and green fully-charged LED are
 * wired directly to the analog battery charge controller / PMU circuit on
 * VBAT/VBUS. They operate autonomously in hardware and cannot be toggled
 * or addressed via software GPIOs.
 * ========================================================================= */

#ifdef __cplusplus
}
#endif

#endif /* HAL_LED_H */
