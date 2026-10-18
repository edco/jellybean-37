/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file startup_anim.c
 * SMK-37 Pro ~2s Startup Animation Sequence
 */

#include "startup_anim.h"
#include "hal_led.h"
#include "hal_display.h"
#include "hal_time.h"
#include "ui_sample.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define ANIM_TOTAL_DURATION_MS 2000
#define NUM_STRIP_BLOCKS 7

static bool anim_running = false;
static uint32_t anim_start_time = 0;

static lv_obj_t *anim_screen = NULL;
static lv_obj_t *strip_blocks[NUM_STRIP_BLOCKS];

/* KRYGCBMK spectrum block colors (Head -> Tail) */
static const uint32_t BLOCK_COLORS[NUM_STRIP_BLOCKS] = {
    0xFF0000, /* Block 0: Red (Head) */
    0xFFFF00, /* Block 1: Yellow */
    0x00FF00, /* Block 2: Green */
    0x00FFFF, /* Block 3: Cyan */
    0x0000FF, /* Block 4: Blue */
    0xFF00FF, /* Block 5: Magenta */
    0x000000  /* Block 6: Black (Tail) */
};

/* Pseudorandom state for sparkle effect */
static uint32_t lfsr_state = 0xACE1u;

static uint32_t anim_prng(void)
{
    lfsr_state = (lfsr_state >> 1) ^ (-(lfsr_state & 1u) & 0xB400u);
    return lfsr_state;
}

/* KRYGCBMK Gradient Color Sampler for Pads:
 * pos_in_strip ranges 0..224
 * 0: Black, 32: Red, 64: Yellow, 96: Green, 128: Cyan, 160: Blue, 192: Magenta, 224: Black
 */
static void get_krygcbmk_rgb(int pos, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (pos < 0 || pos > 224) {
        *r = 0; *g = 0; *b = 0;
        return;
    }

    int seg = pos / 32;
    int rem = pos % 32;
    int interp = (rem * 255) / 32;

    switch (seg) {
    case 0: /* K (0,0,0) -> R (255,0,0) */
        *r = (uint8_t)interp; *g = 0; *b = 0;
        break;
    case 1: /* R (255,0,0) -> Y (255,255,0) */
        *r = 255; *g = (uint8_t)interp; *b = 0;
        break;
    case 2: /* Y (255,255,0) -> G (0,255,0) */
        *r = (uint8_t)(255 - interp); *g = 255; *b = 0;
        break;
    case 3: /* G (0,255,0) -> C (0,255,255) */
        *r = 0; *g = 255; *b = (uint8_t)interp;
        break;
    case 4: /* C (0,255,255) -> B (0,0,255) */
        *r = 0; *g = (uint8_t)(255 - interp); *b = 255;
        break;
    case 5: /* B (0,0,255) -> M (255,0,255) */
        *r = (uint8_t)interp; *g = 0; *b = 255;
        break;
    case 6: /* M (255,0,255) -> K (0,0,0) */
        *r = (uint8_t)(255 - interp); *g = 0; *b = (uint8_t)(255 - interp);
        break;
    default:
        *r = 0; *g = 0; *b = 0;
        break;
    }
}

/* Single Chase Buttons Sequence */
static const SmkBtnLed PHASE1_BUTTONS[6] = {
    SMK_LED_BTN_KNOB_BANK,
    SMK_LED_BTN_NAV_LEFT,
    SMK_LED_BTN_OCTAVE_DOWN,
    SMK_LED_BTN_OCTAVE_UP,
    SMK_LED_BTN_NAV_RIGHT,
    SMK_LED_BTN_FADER_BANK
};

/* Phase 4 Sparkle Button Groups */
static const SmkBtnLed SPARKLE_GRP0[] = { SMK_LED_BTN_ARP, SMK_LED_BTN_PATCH };
static const SmkBtnLed SPARKLE_GRP1[] = { SMK_LED_BTN_NOTE_REPEAT, SMK_LED_BTN_PARA };
static const SmkBtnLed SPARKLE_GRP2[] = { SMK_LED_BTN_SCALE, SMK_LED_BTN_FX };
static const SmkBtnLed SPARKLE_GRP3[] = { SMK_LED_BTN_CHORD, SMK_LED_BTN_SEQ };
static const SmkBtnLed SPARKLE_GRP4[] = { SMK_LED_BTN_GLOBE, SMK_LED_BTN_SEQ_PLAY_RED, SMK_LED_BTN_SEQ_PLAY_GREEN };
static const SmkBtnLed SPARKLE_GRP5[] = { SMK_LED_BTN_BT, SMK_LED_BTN_SEQ_REC };

static const SmkBtnLed *const SPARKLE_GROUPS[6] = {
    SPARKLE_GRP0, SPARKLE_GRP1, SPARKLE_GRP2,
    SPARKLE_GRP3, SPARKLE_GRP4, SPARKLE_GRP5
};
static const uint8_t SPARKLE_GROUP_COUNTS[6] = { 2, 2, 2, 2, 3, 2 };

void startup_anim_init(void)
{
    /* 1. Ensure LCD backlight is ON */
    hal_led_set_backlight(true);

    /* 2. Turn off all button LEDs & pad LEDs at the start */
    hal_led_set_button_mask(0);
    hal_led_set_all_pads(0, 0, 0);
    hal_led_update_pads();

    /* 3. Create black LVGL startup screen */
    anim_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(anim_screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(anim_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(anim_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 4. Create 7 block objects representing the 224px KRYGCBMK strip, positioned off-screen initially */
    for (int i = 0; i < NUM_STRIP_BLOCKS; i++) {
        lv_obj_t *blk = lv_obj_create(anim_screen);
        lv_obj_set_size(blk, 32, 32);
        lv_obj_set_pos(blk, -100, -100);
        lv_obj_set_style_bg_color(blk, lv_color_hex(BLOCK_COLORS[i]), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(blk, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(blk, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(blk, 0, LV_PART_MAIN);
        lv_obj_clear_flag(blk, LV_OBJ_FLAG_SCROLLABLE);
        strip_blocks[i] = blk;
    }

    lv_scr_load(anim_screen);

    anim_start_time = 0; /* Lazy capture on first step */
    anim_running = true;
}

bool startup_anim_step(void)
{
    if (!anim_running) return false;

    uint32_t now = hal_get_ms();
    if (anim_start_time == 0) {
        anim_start_time = now;
    }

    uint32_t elapsed = now - anim_start_time;

    if (elapsed >= ANIM_TOTAL_DURATION_MS) {
        startup_anim_finish();
        return false;
    }

    /* ---------------------------------------------------------------------
     * Phase 1: Single Chase through 6 Button Lamps (0 ms - 500 ms)
     * --------------------------------------------------------------------- */
    if (elapsed < 500) {
        int active_idx = (int)((elapsed * 6) / 500);
        if (active_idx > 5) active_idx = 5;
        for (int i = 0; i < 6; i++) {
            hal_led_set_button(PHASE1_BUTTONS[i], (i == active_idx));
        }
    } else if (elapsed < 1400) {
        /* Turn off Phase 1 buttons once Phase 1 completes */
        for (int i = 0; i < 6; i++) {
            hal_led_set_button(PHASE1_BUTTONS[i], false);
        }
    }

    /* ---------------------------------------------------------------------
     * Phase 2: LCD Strip Perimeter Animation (300 ms - 1300 ms)
     * --------------------------------------------------------------------- */
    if (elapsed >= 300 && elapsed <= 1300) {
        uint32_t phase2_t = elapsed - 300; // 0..1000 ms
        int pulse_head = (int)((phase2_t * (896 + 224)) / 1000);

        for (int i = 0; i < NUM_STRIP_BLOCKS; i++) {
            int pulse_idx = pulse_head - i * 32; // Block 0 is head (enters first)
            lv_obj_t *blk = strip_blocks[i];

            if (pulse_idx >= 0 && pulse_idx <= 928) {
                if (pulse_idx < 208) {
                    /* Right edge heading UP (bottom-right x=208, y=208 -> top-right x=208, y=0) */
                    lv_obj_set_pos(blk, 208, 208 - pulse_idx);
                } else if (pulse_idx < 416) {
                    /* Top edge heading LEFT (top-right x=208, y=0 -> top-left x=0, y=0) */
                    lv_obj_set_pos(blk, 208 - (pulse_idx - 208), 0);
                } else if (pulse_idx < 624) {
                    /* Left edge heading DOWN (top-left x=0, y=0 -> bottom-left x=0, y=208) */
                    lv_obj_set_pos(blk, 0, pulse_idx - 416);
                } else {
                    /* Bottom edge heading RIGHT (bottom-left x=0, y=208 -> exit right x=208, y=208) */
                    lv_obj_set_pos(blk, pulse_idx - 624, 208);
                }
            } else {
                lv_obj_set_pos(blk, -100, -100);
            }
        }
    } else {
        for (int i = 0; i < NUM_STRIP_BLOCKS; i++) {
            if (strip_blocks[i]) {
                lv_obj_set_pos(strip_blocks[i], -100, -100);
            }
        }
    }

    /* ---------------------------------------------------------------------
     * Phase 3: Pads Gradient Chase (1000 ms - 1600 ms)
     * --------------------------------------------------------------------- */
    if (elapsed >= 1000 && elapsed <= 1600) {
        uint32_t phase3_t = elapsed - 1000; // 0..600 ms
        /* Gradient head sweeps from left to right across pad columns 0..7 */
        int pad_head = (int)((phase3_t * (224 + 7 * 28)) / 600);

        for (int col = 0; col < 8; col++) {
            int pos_in_strip = pad_head - col * 28;
            uint8_t r, g, b;
            get_krygcbmk_rgb(pos_in_strip, &r, &g, &b);
            hal_led_set_pad_rgb((uint8_t)col, r, g, b);          // Top row pad
            hal_led_set_pad_rgb((uint8_t)(col + 8), r, g, b);    // Bottom row pad
        }
        hal_led_update_pads();
    } else if (elapsed > 1600 && elapsed < 1700) {
        hal_led_set_all_pads(0, 0, 0);
        hal_led_update_pads();
    }

    /* ---------------------------------------------------------------------
     * Phase 4: Button Random Sparkle Wave (1400 ms - 2000 ms)
     * --------------------------------------------------------------------- */
    if (elapsed >= 1400 && elapsed <= 2000) {
        uint32_t phase4_t = elapsed - 1400; // 0..600 ms
        /* Center position moves smoothly through group indices 0.0 to 5.0 */
        float center = ((float)phase4_t * 6.0f) / 600.0f;

        for (int grp = 0; grp < 6; grp++) {
            float dist = (float)grp - center;
            if (dist < 0.0f) dist = -dist;

            /* Radius decay calculation: intensity drops with distance from wave center */
            uint32_t prob_threshold;
            if (dist < 0.6f) {
                prob_threshold = 200; // ~78% chance of pulse
            } else if (dist < 1.2f) {
                prob_threshold = 100; // ~39% chance of pulse
            } else if (dist < 1.8f) {
                prob_threshold = 40;  // ~15% chance of pulse
            } else {
                prob_threshold = 0;   // Outside radius
            }

            for (uint8_t b = 0; b < SPARKLE_GROUP_COUNTS[grp]; b++) {
                bool pulse = ((anim_prng() % 256) < prob_threshold);
                hal_led_set_button(SPARKLE_GROUPS[grp][b], pulse);
            }
        }
    }

    return true;
}

void startup_anim_finish(void)
{
    if (!anim_running) return;

    anim_running = false;

    /* Extinguish all button & pad LEDs */
    hal_led_set_button_mask(0);
    hal_led_set_all_pads(0, 0, 0);
    hal_led_update_pads();

    /* Switch back to main synth UI screen */
    ui_sample_show_screen();

    /* Clean up startup screen */
    if (anim_screen) {
        lv_obj_del(anim_screen);
        anim_screen = NULL;
        for (int i = 0; i < NUM_STRIP_BLOCKS; i++) {
            strip_blocks[i] = NULL;
        }
    }
}

bool startup_anim_is_running(void)
{
    return anim_running;
}
