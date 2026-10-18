/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file sim_input.h
 * PC Keyboard to SMK-37 Pro Hardware Controls Mapper
 */

#ifndef SIM_INPUT_H
#define SIM_INPUT_H

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

void sim_input_init(void);
void sim_input_handle_event(const SDL_Event *event);

#ifdef __cplusplus
}
#endif

#endif /* SIM_INPUT_H */
