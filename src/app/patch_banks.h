/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file patch_banks.h
 * Bank and Patch metadata for Synthesizer Engines (Dexed, MCUno-60)
 */

#ifndef PATCH_BANKS_H
#define PATCH_BANKS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_ENGINE_DEXED    0
#define SYNTH_ENGINE_MCUNO60  1
#define SYNTH_NUM_ENGINES     2

#define PATCH_NUM_BANKS 10
#define PATCH_NUM_PATCHES_PER_BANK 32

/**
 * Get total number of engines.
 */
uint8_t patch_banks_get_num_engines(void);

/**
 * Get display name of engine (0..SYNTH_NUM_ENGINES-1).
 */
const char *patch_banks_get_engine_name(uint8_t engine_idx);

/**
 * Get number of banks for specified engine.
 */
uint8_t patch_banks_get_num_banks_for_engine(uint8_t engine_idx);

/**
 * Get number of patches in a bank for specified engine.
 *
 * Bank sizes are not uniform: MCUno-60 merges factory bank pairs into 64-patch
 * banks and has a short benchmark bank, while Dexed uses a fixed 32. Callers
 * doing patch navigation must use this rather than PATCH_NUM_PATCHES_PER_BANK,
 * which only describes the Dexed layout.
 */
uint8_t patch_banks_get_num_patches_for_engine(uint8_t engine_idx, uint8_t bank_idx);

/**
 * Get the display name of a bank for specified engine.
 */
const char *patch_banks_get_bank_name_for_engine(uint8_t engine_idx, uint8_t bank_idx);

/**
 * Get the display name of a patch within a bank for specified engine.
 */
const char *patch_banks_get_patch_name_for_engine(uint8_t engine_idx, uint8_t bank_idx, uint8_t patch_idx);

/**
 * Get the display name of a bank for default/active engine (Dexed).
 */
const char *patch_banks_get_bank_name(uint8_t bank_idx);

/**
 * Get the display name of a patch within a bank for default/active engine (Dexed).
 */
const char *patch_banks_get_patch_name(uint8_t bank_idx, uint8_t patch_idx);

#ifdef __cplusplus
}
#endif

#endif /* PATCH_BANKS_H */
