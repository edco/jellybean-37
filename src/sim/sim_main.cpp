/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file sim_main.cpp
 * Host Simulation Harness Entry Point (x86_64 SDL2)
 */

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif
#include <cstdio>
#include "lvgl.h"
#include "hal_display.h"
#include "hal_time.h"
#include "app_main.h"
#include "sim_display.h"
#include "sim_audio.h"
#include "sim_input.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    std::printf("====================================================\n");
    std::printf("  SMK-37 Pro Hardware Simulator (x86_64 SDL2)\n");
    std::printf("  Display: 240x240 ST7789 (scaled 3x to 720x720)\n");
    std::printf("  Audio: 48 kHz Stereo 16-bit Signed (128 samples)\n");
    std::printf("  Keybed: 37 Keys ('zsxdcvgbhnjm,l.;/q2w3e4rt6y7ui9o0p-[]')\n");
    std::printf("  Pads: Shift + (Top: ASDFGHJK, Bottom: ZXCVBNM<)\n");
    std::printf("  Encoders/Faders/Wheels: Alt + Keys\n");
    std::printf("  Control Buttons: Ctrl + Keys\n");
    std::printf("====================================================\n");

    /* 1. Initialize SDL2 subsystems */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "Failed to initialize SDL2: %s\n", SDL_GetError());
        return 1;
    }

    /* 2. Initialize LVGL v8 Core */
    lv_init();

    /* 3. Initialize Display Simulation Backend */
    if (sim_display_init() != 0) {
        std::fprintf(stderr, "Failed to initialize simulation display\n");
        SDL_Quit();
        return 1;
    }

    /* 4. Initialize Input Mapper */
    sim_input_init();

    /* 5. Initialize Shared Application (Synth Engine, UI Screen, Audio) */
    app_init();

    /* 6. Simulator Main Loop */
    bool running = true;
    uint32_t last_tick = SDL_GetTicks();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                break;
            }
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                /* Allow Escape key to exit simulator */
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                    break;
                }
                sim_input_handle_event(&event);
            }
        }

        /* Update LVGL system tick */
        uint32_t current_tick = SDL_GetTicks();
        uint32_t elapsed = current_tick - last_tick;
        if (elapsed > 0) {
            lv_tick_inc(elapsed);
            last_tick = current_tick;
        }

        /* Process LVGL timers and render updates */
        lv_timer_handler();

        /* Advance shared application logic */
        app_step();

        /* Yield CPU (~100-200 FPS cap) */
        SDL_Delay(5);
    }

    std::printf("Shutting down simulator...\n");
    sim_audio_shutdown();
    sim_display_shutdown();
    SDL_Quit();
    return 0;
}
