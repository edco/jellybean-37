/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file hal_time.h
 * Hardware Abstraction Layer for System Timing and Delays
 */

#ifndef HAL_TIME_H
#define HAL_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize platform hardware timers.
 */
void hal_time_init(void);

/**
 * Get current system time in milliseconds.
 */
uint32_t hal_get_ms(void);

/**
 * Delay execution for specified milliseconds.
 */
void hal_delay_ms(uint32_t ms);

/**
 * Delay execution for specified microseconds.
 */
void hal_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* HAL_TIME_H */
