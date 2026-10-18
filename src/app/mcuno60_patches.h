/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcuno60_patches.h
 * Factory patch data for MCUno-60 Synthesizer
 * Generated from factory_patches.ts
 */

#ifndef MCUNO60_PATCHES_H
#define MCUNO60_PATCHES_H

#include <stdint.h>
#include "voice.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Factory bank layout as generated from factory_patches.ts. The user-visible
 * patch list is a remapped view over this table (see mcuno60_patches.c). */
#define MCUNO60_FACTORY_BANKS 8
#define MCUNO60_FACTORY_PATCHES_PER_BANK 32

/* User-visible layout. Banks 1..4 each merge two factory banks into 64 patches;
 * bank 0 is the benchmark bank and is shorter. */
#define MCUNO60_NUM_BANKS 5
#define MCUNO60_BENCH_BANK 0
#define MCUNO60_BENCH_PATCHES 4
#define MCUNO60_PATCHES_PER_BANK 64

const char *mcuno60_get_bank_name(uint8_t bank_idx);
const char *mcuno60_get_patch_name(uint8_t bank_idx, uint8_t patch_idx);
const JunoEngineParams *mcuno60_get_patch_params(uint8_t bank_idx, uint8_t patch_idx);
uint8_t mcuno60_get_bank_patch_count(uint8_t bank_idx);

#ifdef __cplusplus
}
#endif

#endif /* MCUNO60_PATCHES_H */
