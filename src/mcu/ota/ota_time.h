/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef OTA_TIME_H
#define OTA_TIME_H

#include "ota_sys.h"

/* Timer 4 increments at 24 MHz */
#define TIMER_TICKS_PER_US      24u
#define TIMER_TICKS_PER_MS      24000u

static inline void time_init(void)
{
    if ((JL_TIMER4->CON & 0xFu) == ((2u << 2) | 1u)) {
        return;
    }
    JL_TIMER4->CON = 0;
    JL_TIMER4->PRD = 0xFFFFFFFFu;
    JL_TIMER4->CNT = 0;
    JL_TIMER4->CON = (2u << 2) | 1u | (1u << 14); /* 24 MHz oscillator, run, clear pending */
}

static inline void time_delay_cycles(uint32_t count)
{
    volatile uint32_t c = count;
    while (c--) {
        __asm__ volatile("nop");
    }
}

static inline uint32_t time_get_ticks(void)
{
    return JL_TIMER4->CNT;
}

static inline uint32_t time_get_ms(void)
{
    return JL_TIMER4->CNT / TIMER_TICKS_PER_MS;
}

static inline void time_delay_us(uint32_t us)
{
    uint32_t start = JL_TIMER4->CNT;
    uint32_t target = us * TIMER_TICKS_PER_US;
    while ((uint32_t)(JL_TIMER4->CNT - start) < target) {
        __asm__ volatile("nop");
    }
}

static inline void time_delay_ms(uint32_t ms)
{
    while (ms--) {
        time_delay_us(1000);
    }
}

#endif /* OTA_TIME_H */
