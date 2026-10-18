/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file sim_audio.h
 * SDL2 Audio Simulation Backend
 */

#ifndef SIM_AUDIO_H
#define SIM_AUDIO_H

#include "hal_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

void sim_audio_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* SIM_AUDIO_H */
