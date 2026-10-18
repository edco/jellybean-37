/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file sim_input.cpp
 * PC Keyboard to SMK-37 Pro Hardware Controls Mapper
 */

#include "sim_input.h"
#include "hal_input.h"
#include <algorithm>
#include <cstdint>

namespace {

/* 37 chromatic keys mapping:
 * "zsxdcvgbhnjm,l.;/q2w3e4rt6y7ui9o0p-[]"
 */
static const SDL_Keycode KEYBED_MAP[SMK_NUM_KEYS] = {
    SDLK_z, SDLK_s, SDLK_x, SDLK_d, SDLK_c, SDLK_v, SDLK_g, SDLK_b,
    SDLK_h, SDLK_n, SDLK_j, SDLK_m, SDLK_COMMA, SDLK_l, SDLK_PERIOD,
    SDLK_SEMICOLON, SDLK_SLASH, SDLK_q, SDLK_2, SDLK_w, SDLK_3, SDLK_e,
    SDLK_4, SDLK_r, SDLK_t, SDLK_6, SDLK_y, SDLK_7, SDLK_u, SDLK_i,
    SDLK_9, SDLK_o, SDLK_0, SDLK_p, SDLK_MINUS, SDLK_LEFTBRACKET,
    SDLK_RIGHTBRACKET
};

/* Pads Top Row (Pads 0..7): ASDFGHJK */
static const SDL_Keycode PADS_TOP[8] = {
    SDLK_a, SDLK_s, SDLK_d, SDLK_f, SDLK_g, SDLK_h, SDLK_j, SDLK_k
};

/* Pads Bottom Row (Pads 8..15): ZXCVBNM< */
static const SDL_Keycode PADS_BOTTOM[8] = {
    SDLK_z, SDLK_x, SDLK_c, SDLK_v, SDLK_b, SDLK_n, SDLK_m, SDLK_COMMA
};

/* 8 Encoders Up: 12345678 */
static const SDL_Keycode ENC_UP[SMK_NUM_ENCODERS] = {
    SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7, SDLK_8
};

/* 8 Encoders Down: qwertyui */
static const SDL_Keycode ENC_DOWN[SMK_NUM_ENCODERS] = {
    SDLK_q, SDLK_w, SDLK_e, SDLK_r, SDLK_t, SDLK_y, SDLK_u, SDLK_i
};

/* 4 Faders Up: 90-= */
static const SDL_Keycode FADER_UP[SMK_NUM_FADERS] = {
    SDLK_9, SDLK_0, SDLK_MINUS, SDLK_EQUALS
};

/* 4 Faders Down: op[] */
static const SDL_Keycode FADER_DOWN[SMK_NUM_FADERS] = {
    SDLK_o, SDLK_p, SDLK_LEFTBRACKET, SDLK_RIGHTBRACKET
};

/* State tracking for continuous controls */
static int16_t pitch_val = 0;       /* -8192..8191 */
static uint8_t mod_val = 0;         /* 0..127 */
static uint8_t fader_vals[SMK_NUM_FADERS] = {64, 64, 64, 64};

/* Track active keybed notes pressed by keycode */
static int key_to_note[SDL_NUM_SCANCODES];
static int key_to_pad[SDL_NUM_SCANCODES];

} // namespace

extern "C" {

void sim_input_init(void)
{
    for (int i = 0; i < SDL_NUM_SCANCODES; i++) {
        key_to_note[i] = -1;
        key_to_pad[i] = -1;
    }
}

void sim_input_handle_event(const SDL_Event *event)
{
    if (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP) {
        return;
    }

    bool is_down = (event->type == SDL_KEYDOWN);
    bool is_repeat = (event->key.repeat != 0);
    SDL_Keycode key = event->key.keysym.sym;
    SDL_Scancode scancode = event->key.keysym.scancode;
    uint16_t mod = event->key.keysym.mod;

    bool shift_held = (mod & KMOD_SHIFT) != 0;
    bool alt_held   = (mod & KMOD_ALT) != 0;
    bool ctrl_held  = (mod & KMOD_CTRL) != 0;

    /* Handle key releases first using stored active state */
    if (!is_down) {
        if (key_to_note[scancode] >= 0) {
            hal_input_dispatch_key(static_cast<uint8_t>(key_to_note[scancode]), false, 0);
            key_to_note[scancode] = -1;
        }
        if (key_to_pad[scancode] >= 0) {
            hal_input_dispatch_pad(static_cast<uint8_t>(key_to_pad[scancode]), false, 0);
            key_to_pad[scancode] = -1;
        }
        return;
    }

    /* 1. Control Buttons: Ctrl Held */
    if (ctrl_held) {
        if (is_repeat) return;
        SmkButton btn = SMK_BTN_COUNT;
        switch (key) {
            case SDLK_q: btn = SMK_BTN_KNOB_BANK; break;
            case SDLK_w: btn = SMK_BTN_FADER_BANK; break;
            case SDLK_a: btn = SMK_BTN_NAV_LEFT; break;
            case SDLK_s: btn = SMK_BTN_NAV_RIGHT; break;
            case SDLK_z: btn = SMK_BTN_OCTAVE_DOWN; break;
            case SDLK_x: btn = SMK_BTN_OCTAVE_UP; break;
            case SDLK_d: btn = SMK_BTN_ARP; break;
            case SDLK_f: btn = SMK_BTN_NOTE_REPEAT; break;
            case SDLK_g: btn = SMK_BTN_SCALE; break;
            case SDLK_h: btn = SMK_BTN_CHORD; break;
            case SDLK_j: btn = SMK_BTN_GLOBE; break;
            case SDLK_k: btn = SMK_BTN_BT; break;
            case SDLK_c: btn = SMK_BTN_PATCH; break;
            case SDLK_v: btn = SMK_BTN_PARA; break;
            case SDLK_b: btn = SMK_BTN_FX; break;
            case SDLK_n: btn = SMK_BTN_SEQ; break;
            case SDLK_m: btn = SMK_BTN_SEQ_PLAY; break;
            case SDLK_COMMA: btn = SMK_BTN_SEQ_REC; break;
            default: break;
        }
        if (btn < SMK_BTN_COUNT) {
            hal_input_dispatch_button(btn, true);
        }
        return;
    }

    /* 2. Encoders, Wheels, Faders: Alt Held */
    if (alt_held) {
        /* Encoders 1..8 Up / Down */
        for (int i = 0; i < SMK_NUM_ENCODERS; i++) {
            if (key == ENC_UP[i]) {
                hal_input_dispatch_encoder(i, +1);
                return;
            }
            if (key == ENC_DOWN[i]) {
                hal_input_dispatch_encoder(i, -1);
                return;
            }
        }

        /* Pitch & Mod Wheels */
        if (key == SDLK_a) { // Pitch Up
            pitch_val = std::min(8191, pitch_val + 1024);
            hal_input_dispatch_wheel(SMK_WHEEL_PITCH, pitch_val);
            return;
        }
        if (key == SDLK_z) { // Pitch Down
            pitch_val = std::max(-8192, pitch_val - 1024);
            hal_input_dispatch_wheel(SMK_WHEEL_PITCH, pitch_val);
            return;
        }
        if (key == SDLK_s) { // Mod Up
            mod_val = static_cast<uint8_t>(std::min(127, mod_val + 10));
            hal_input_dispatch_wheel(SMK_WHEEL_MOD, mod_val);
            return;
        }
        if (key == SDLK_x) { // Mod Down
            mod_val = static_cast<uint8_t>(std::max(0, mod_val - 10));
            hal_input_dispatch_wheel(SMK_WHEEL_MOD, mod_val);
            return;
        }

        /* Faders 1..4 Up / Down */
        for (int i = 0; i < SMK_NUM_FADERS; i++) {
            if (key == FADER_UP[i]) {
                fader_vals[i] = static_cast<uint8_t>(std::min(127, fader_vals[i] + 8));
                hal_input_dispatch_fader(i, fader_vals[i]);
                return;
            }
            if (key == FADER_DOWN[i]) {
                fader_vals[i] = static_cast<uint8_t>(std::max(0, fader_vals[i] - 8));
                hal_input_dispatch_fader(i, fader_vals[i]);
                return;
            }
        }
        return;
    }

    /* 3. 16 Pads: Shift Held */
    if (shift_held) {
        if (is_repeat) return;
        /* Top Row (0..7): ASDFGHJK */
        for (int i = 0; i < 8; i++) {
            if (key == PADS_TOP[i]) {
                key_to_pad[scancode] = i;
                hal_input_dispatch_pad(i, true, 115);
                return;
            }
        }
        /* Bottom Row (8..15): ZXCVBNM< */
        for (int i = 0; i < 8; i++) {
            if (key == PADS_BOTTOM[i] || (i == 7 && key == SDLK_LESS)) {
                int pad_idx = 8 + i;
                key_to_pad[scancode] = pad_idx;
                hal_input_dispatch_pad(pad_idx, true, 115);
                return;
            }
        }
        return;
    }

    /* 4. 37 Chromatic Keybed Keys: No modifiers held */
    if (is_repeat) return;
    for (int i = 0; i < SMK_NUM_KEYS; i++) {
        if (key == KEYBED_MAP[i]) {
            key_to_note[scancode] = i;
            hal_input_dispatch_key(static_cast<uint8_t>(i), true, 100);
            return;
        }
    }
}

} // extern "C"
