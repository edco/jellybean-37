/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file hal_audio.h
 * Hardware Abstraction Layer for Audio Output (DAC / I2S / SDL2 Audio)
 */

#ifndef HAL_AUDIO_H
#define HAL_AUDIO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_DEFAULT_SAMPLE_RATE   48000
#define AUDIO_DEFAULT_CHANNELS      2       /* Stereo (L, R interleaved) */
#define AUDIO_DEFAULT_BUFFER_FRAMES 64

/**
 * Audio block streaming callback: fills interleaved 16-bit signed stereo samples.
 * @param buffer Output buffer of size (frames * 2 * sizeof(int16_t))
 * @param frames Number of stereo sample pairs to generate
 */
typedef void (*HalAudioCallback)(int16_t *buffer, size_t frames);
 
/**
 * Initialize audio subsystem.
 * @param sample_rate Audio sample rate in Hz (typically 48000)
 * @param callback Callback polled whenever the audio device needs more samples
 * @return 0 on success, negative error code on failure.
 */
int hal_audio_init(uint32_t sample_rate, HalAudioCallback callback);

/**
 * Start or unpause audio playback.
 */
void hal_audio_start(void);

/**
 * Stop or pause audio playback.
 */
void hal_audio_stop(void);

/**
 * Set master output volume (0..100).
 */
void hal_audio_set_volume(uint8_t volume);

/**
 * Diagnostic: number of audio blocks rendered. Advances only when the audio
 * backend is actively requesting samples (DMA/IRQ liveness indicator).
 */
uint32_t hal_audio_get_block_count(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_AUDIO_H */
