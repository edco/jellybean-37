/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file startup_anim.h
 * SMK-37 Pro ~2s Startup Animation Module
 */

#ifndef STARTUP_ANIM_H
#define STARTUP_ANIM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize and begin the startup animation sequence.
 * Clears all lamps, turns on LCD backlight, and creates the startup screen canvas.
 */
void startup_anim_init(void);

/**
 * Advance the startup animation state machine.
 * Call periodically in the main loop (e.g. from app_step).
 * @return true if animation is still in progress, false when complete.
 */
bool startup_anim_step(void);

/**
 * Clean up startup animation resources and prepare for main UI transition.
 */
void startup_anim_finish(void);

/**
 * Check whether startup animation is currently running.
 * @return true if running, false if finished.
 */
bool startup_anim_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* STARTUP_ANIM_H */
