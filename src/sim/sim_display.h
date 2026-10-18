/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file sim_display.h
 * SDL2 Hardware Simulation Display Backend
 */

#ifndef SIM_DISPLAY_H
#define SIM_DISPLAY_H

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif
#include "hal_display.h"

#ifdef __cplusplus
extern "C" {
#endif

int sim_display_init(void);
void sim_display_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_DISPLAY_H */
