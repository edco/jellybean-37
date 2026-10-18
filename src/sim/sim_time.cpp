/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file sim_time.cpp
 * Host Timing Implementation using SDL2
 */

#include "hal_time.h"
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

extern "C" {

void hal_time_init(void)
{
    /* SDL timer subsystem is initialized with SDL_Init */
}

uint32_t hal_get_ms(void)
{
    return SDL_GetTicks();
}

void hal_delay_ms(uint32_t ms)
{
    SDL_Delay(ms);
}

void hal_delay_us(uint32_t us)
{
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t ticks_to_wait = (freq * us) / 1000000ULL;
    uint64_t start = SDL_GetPerformanceCounter();
    while ((SDL_GetPerformanceCounter() - start) < ticks_to_wait) {
        /* Busy wait for microsecond accuracy */
    }
}

} // extern "C"
