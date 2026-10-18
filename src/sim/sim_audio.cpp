/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file sim_audio.cpp
 * SDL2 Audio Device Backend (16-bit stereo @ 48 kHz, buffer size 128 samples)
 */

#include "sim_audio.h"
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif
#include <cstdio>

namespace {

static SDL_AudioDeviceID audio_device = 0;
static HalAudioCallback user_callback = nullptr;
static volatile uint32_t audio_blocks = 0;

static void sdl_audio_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    size_t frames = static_cast<size_t>(len) / (2 * sizeof(int16_t));
    audio_blocks++;
    if (user_callback) {
        user_callback(reinterpret_cast<int16_t *>(stream), frames);
    } else {
        SDL_memset(stream, 0, len);
    }
}

} // namespace

extern "C" {

int hal_audio_init(uint32_t sample_rate, HalAudioCallback callback)
{
    user_callback = callback;

    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq = (sample_rate > 0) ? sample_rate : AUDIO_DEFAULT_SAMPLE_RATE;
    desired.format = AUDIO_S16SYS;
    desired.channels = AUDIO_DEFAULT_CHANNELS;
    desired.samples = AUDIO_DEFAULT_BUFFER_FRAMES; /* 128 samples */
    desired.callback = sdl_audio_callback;
    desired.userdata = nullptr;

    audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (audio_device == 0) {
        std::fprintf(stderr, "SDL_OpenAudioDevice Error: %s\n", SDL_GetError());
        return -1;
    }

    return 0;
}

void hal_audio_start(void)
{
    if (audio_device != 0) {
        SDL_PauseAudioDevice(audio_device, 0); // Unpause
    }
}

void hal_audio_stop(void)
{
    if (audio_device != 0) {
        SDL_PauseAudioDevice(audio_device, 1); // Pause
    }
}

void hal_audio_set_volume(uint8_t volume)
{
    (void)volume;
    /* Volume can be adjusted in software or master mixer */
}

uint32_t hal_audio_get_block_count(void)
{
    return audio_blocks;
}

void sim_audio_shutdown(void)
{
    if (audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }
}

} // extern "C"
