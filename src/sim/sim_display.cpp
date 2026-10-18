/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file sim_display.cpp
 * SDL2 Simulation Display Driver for LVGL (240x240 ST7789 Emulation)
 */

#include "sim_display.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace {

static SDL_Window *sim_window = nullptr;
static SDL_Renderer *sim_renderer = nullptr;
static SDL_Texture *sim_texture = nullptr;

static lv_disp_draw_buf_t disp_draw_buf;
static lv_color_t disp_buf1[LCD_WIDTH * 40];
static lv_disp_drv_t disp_drv;

static void sim_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

    SDL_Rect rect;
    rect.x = area->x1;
    rect.y = area->y1;
    rect.w = static_cast<int>(w);
    rect.h = static_cast<int>(h);

    /* Stream RGB565 directly into SDL texture */
    SDL_UpdateTexture(sim_texture, &rect, color_p, w * sizeof(uint16_t));

    if (lv_disp_flush_is_last(drv)) {
        SDL_RenderClear(sim_renderer);
        SDL_RenderCopy(sim_renderer, sim_texture, nullptr, nullptr);
        SDL_RenderPresent(sim_renderer);
    }

    lv_disp_flush_ready(drv);
}

} // namespace

extern "C" {

int hal_display_init(void)
{
    /* 3x integer scaling: 240x240 -> 720x720 */
    const int SCALE = 3;
    const int WIN_W = LCD_WIDTH * SCALE;
    const int WIN_H = LCD_HEIGHT * SCALE;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // Nearest neighbor for pixel-perfect LCD look

    sim_window = SDL_CreateWindow(
        "SMK-37 Pro Hardware Simulator (240x240 ST7789)",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WIN_W,
        WIN_H,
        SDL_WINDOW_SHOWN
    );

    if (!sim_window) {
        std::fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        return -1;
    }

    sim_renderer = SDL_CreateRenderer(
        sim_window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!sim_renderer) {
        /* Fallback to software renderer (e.g. headless / virtual display) */
        sim_renderer = SDL_CreateRenderer(sim_window, -1, SDL_RENDERER_SOFTWARE);
    }

    if (!sim_renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
        return -1;
    }

    SDL_RenderSetLogicalSize(sim_renderer, LCD_WIDTH, LCD_HEIGHT);

    sim_texture = SDL_CreateTexture(
        sim_renderer,
        SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING,
        LCD_WIDTH,
        LCD_HEIGHT
    );

    if (!sim_texture) {
        std::fprintf(stderr, "SDL_CreateTexture Error: %s\n", SDL_GetError());
        return -1;
    }

    /* Initialize LVGL draw buffer */
    lv_disp_draw_buf_init(&disp_draw_buf, disp_buf1, nullptr, LCD_WIDTH * 40);

    /* Register LVGL display driver */
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = sim_disp_flush;
    disp_drv.draw_buf = &disp_draw_buf;
    lv_disp_drv_register(&disp_drv);

    return 0;
}

void hal_display_backlight(uint8_t enable)
{
    (void)enable;
    /* In desktop simulation, backlight is always active */
}

int sim_display_init(void)
{
    return hal_display_init();
}

void sim_display_shutdown(void)
{
    if (sim_texture) {
        SDL_DestroyTexture(sim_texture);
        sim_texture = nullptr;
    }
    if (sim_renderer) {
        SDL_DestroyRenderer(sim_renderer);
        sim_renderer = nullptr;
    }
    if (sim_window) {
        SDL_DestroyWindow(sim_window);
        sim_window = nullptr;
    }
}

} // extern "C"
