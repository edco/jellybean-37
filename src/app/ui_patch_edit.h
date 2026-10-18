/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file ui_patch_edit.h
 * LVGL v8 Patch Editing Screen for MCUno-32 / MCUno-60 (240x240)
 */

#ifndef UI_PATCH_EDIT_H
#define UI_PATCH_EDIT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_PATCH_EDIT_NUM_PAGES 4

/**
 * Initialize patch editing UI screen and child widgets.
 */
void ui_patch_edit_init(void);

/**
 * Make the patch edit screen the active LVGL screen.
 */
void ui_patch_edit_show(void);

/**
 * Hide the patch edit screen and restore the main synth dashboard.
 */
void ui_patch_edit_hide(void);

/**
 * Toggle between main dashboard and patch edit screen.
 */
void ui_patch_edit_toggle(void);

/**
 * Check if patch edit screen is currently active.
 */
bool ui_patch_edit_is_active(void);

/**
 * Navigate to next (+1) or previous (-1) editing page.
 */
void ui_patch_edit_nav_page(int8_t dir);

/**
 * Handle encoder turn events (Knobs 0..7).
 */
void ui_patch_edit_on_encoder(uint8_t enc_index, int8_t delta);

/**
 * Update octave indicator on the patch edit header.
 */
void ui_patch_edit_set_octave(int8_t octave);

/**
 * Load parameters from the specified bank and patch into the editor.
 * Cleanly uses mcuno60_get_patch_params() without duplicating tables.
 */
void ui_patch_edit_load_patch(uint8_t bank_idx, uint8_t patch_idx);

/**
 * Periodic update hook (timer decay for value popup banner).
 */
void ui_patch_edit_update(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_PATCH_EDIT_H */
