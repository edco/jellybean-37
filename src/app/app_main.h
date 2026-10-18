/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file app_main.h
 * Shared application initialization and main logic
 */

#ifndef APP_MAIN_H
#define APP_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize high-level application (synth engine, audio, UI, input listeners).
 */
void app_init(void);

/**
 * Register input event listeners. Called before the keybed scanner starts so
 * no early hardware events are dropped.
 */
void app_register_input(void);

/**
 * Periodic application tick (called from main UI loop).
 */
void app_step(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_H */
