/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file ui_sample.h
 * LVGL v8 Synthesizer Dashboard UI for SMK-37 Pro (240x240)
 */

#ifndef UI_SAMPLE_H
#define UI_SAMPLE_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "hal_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize and construct the LVGL sample screen.
 */
void ui_sample_init(void);

/**
 * Make the main sample synth screen active.
 */
void ui_sample_show_screen(void);

/**
 * Periodic UI update called from main timer loop (updates meters, FPS, etc.).
 */
void ui_sample_update(void);

/**
 * Visual feedback hooks for input events.
 */
void ui_sample_set_key(uint8_t key_index, bool pressed);
void ui_sample_set_pad(uint8_t pad_index, bool pressed, uint8_t velocity);
void ui_sample_set_pad_pressure(uint8_t pad_index, uint8_t pressure);
void ui_sample_set_encoder(uint8_t enc_index, int8_t delta);
void ui_sample_set_fader(uint8_t fader_index, uint8_t value);
void ui_sample_set_wheel(SmkWheel wheel, int16_t value);
void ui_sample_set_button(SmkButton button, bool pressed);
void ui_sample_set_octave(int8_t octave);

void ui_sample_set_engine_bank_patch(uint8_t engine, uint8_t bank, uint8_t patch, const char *engine_name, const char *bank_name, const char *patch_name, bool pending);
void ui_sample_set_bank_patch(uint8_t bank, uint8_t patch, const char *bank_name, const char *patch_name, bool pending);
void ui_sample_show_volume(uint8_t volume);
void ui_sample_show_limiter(float t, float m, bool is_thresh);
void ui_sample_clear_volume_overlay(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SAMPLE_H */
