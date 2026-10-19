/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file app_main.c
 * Shared application initialization and event dispatch
 */

#include "app_main.h"
#include "hal_audio.h"
#include "hal_midi.h"
#include "hal_input.h"
#include "hal_time.h"
#include "hal_synth.h"
#include "hal_diag.h"
#include "hal_led.h"
#include "ui_sample.h"
#include "ui_patch_edit.h"
#include "startup_anim.h"
#include "patch_banks.h"
#include "synth_engine.h"
#include "soft_limiter.h"
#include <math.h>

static int8_t current_octave = 0;
static uint8_t current_volume = 100;
static uint8_t current_mod_wheel = 0;
static uint8_t current_engine = 0;
static uint8_t current_bank = 0;
static uint8_t current_patch = 0;

static float current_limiter_t = SOFT_LIMITER_DEFAULT_THRESHOLD;
static float current_limiter_m = SOFT_LIMITER_DEFAULT_MAX;
static uint32_t limiter_change_time = 0;
static bool limiter_showing = false;

/** Deferred until after the startup animation, which clears every panel lamp. */

static uint32_t patch_change_time = 0;
static bool patch_pending = false;

static uint32_t vol_change_time = 0;
static bool vol_showing = false;

static uint8_t active_key_notes[SMK_NUM_KEYS] = {0};

static void sync_shared_params(void)
{
    hal_synth_set_volume(current_volume);
    hal_synth_mod_wheel(current_mod_wheel);
    hal_synth_set_octave(current_octave);
}

/* Audio half of a key event. Runs on the scan task, synchronously at the
 * moment the contact edge is detected (see hal_input_init_audio), so note-on
 * latency is one scan interval rather than a GUI wake period. Touches no UI. */
static void on_key_audio(uint8_t key_index, bool pressed, uint8_t velocity)
{
    if (key_index >= SMK_NUM_KEYS) return;

    if (pressed) {
        int midi_note = 48 + (current_octave * 12) + key_index;
        if (midi_note < 0) midi_note = 0;
        if (midi_note > 127) midi_note = 127;
        active_key_notes[key_index] = (uint8_t)midi_note;

        hal_synth_note_on((uint8_t)midi_note, velocity ? velocity : 100);
        hal_midi_send_note_on(0, (uint8_t)midi_note, velocity ? velocity : 100);
    } else {
        hal_synth_note_off(active_key_notes[key_index]);
        hal_midi_send_note_off(0, active_key_notes[key_index], 0);
    }
}

/* Visual half of a key event. Runs on vGuiTask, which is the only task
 * allowed to touch LVGL. Must NOT re-issue the note call -- on_key_audio
 * already did that on the scan task. */
static void on_key_event(uint8_t key_index, bool pressed, uint8_t velocity)
{
    (void)velocity;
    ui_sample_set_key(key_index, pressed);
}

static void on_pad_audio(uint8_t pad_index, bool pressed, uint8_t velocity)
{
    /* Pads 0..15 mapped to General MIDI percussion notes (36..51) */
    uint8_t pad_note = 36 + pad_index;

    if (pressed) {
        hal_synth_note_on(pad_note, velocity ? velocity : 115);
        hal_midi_send_note_on(9, pad_note, velocity ? velocity : 115);
    } else {
        hal_synth_note_off(pad_note);
        hal_midi_send_note_off(9, pad_note, 0);
    }
}

static void on_pad_event(uint8_t pad_index, bool pressed, uint8_t velocity)
{
    ui_sample_set_pad(pad_index, pressed, velocity);
    if (pressed) {
        uint8_t v = velocity ? velocity : 100;
        uint8_t b = (uint8_t)((v * 255) / 127);
        switch (pad_index / 4) {
        case 0: /* Cyan */
            hal_led_set_pad_rgb(pad_index, 0, b, b);
            break;
        case 1: /* Magenta */
            hal_led_set_pad_rgb(pad_index, b, 0, (uint8_t)(b / 2));
            break;
        case 2: /* Amber */
            hal_led_set_pad_rgb(pad_index, b, (uint8_t)(b / 2), 0);
            break;
        default: /* Blue */
            hal_led_set_pad_rgb(pad_index, 0, (uint8_t)(b / 4), b);
            break;
        }
    } else {
        hal_led_set_pad_rgb(pad_index, 0, 0, 0);
    }
}

static void on_pad_pressure_audio(uint8_t pad_index, uint8_t pressure)
{
    hal_synth_aftertouch(pressure);
    hal_midi_send_channel_pressure(0, pressure);
    hal_midi_send_poly_pressure(9, 36 + pad_index, pressure);
}

static void on_pad_pressure_event(uint8_t pad_index, uint8_t pressure)
{
    ui_sample_set_pad_pressure(pad_index, pressure);
}

static void on_encoder_event(uint8_t enc_index, int8_t delta)
{
    if (ui_patch_edit_is_active()) {
        ui_patch_edit_on_encoder(enc_index, delta);
        return;
    }

    switch (enc_index) {
    case 0: /* K1: Master Volume */
    {
        int val = (int)current_volume + delta * 4;
        if (val < 0) val = 0;
        if (val > 127) val = 127;
        current_volume = (uint8_t)val;
        sync_shared_params();
        hal_audio_set_volume((uint8_t)((current_volume * 100) / 127));

        vol_change_time = hal_get_ms();
        vol_showing = true;
        ui_sample_show_volume(current_volume);
        break;
    }
    case 1: /* K2: Engine Select */
    {
        uint8_t num_engines = patch_banks_get_num_engines();
        int val = (int)current_engine + delta;
        if (val < 0) val = 0;
        if (val >= num_engines) val = num_engines - 1;

        if (current_engine != (uint8_t)val) {
            current_engine = (uint8_t)val;
            current_bank = 0;
            current_patch = 0;
        }
        patch_change_time = hal_get_ms();
        patch_pending = true;

        ui_sample_set_engine_bank_patch(current_engine, current_bank, current_patch,
                                         patch_banks_get_engine_name(current_engine),
                                         patch_banks_get_bank_name_for_engine(current_engine, current_bank),
                                         patch_banks_get_patch_name_for_engine(current_engine, current_bank, current_patch),
                                         true);
        break;
    }
    case 2: /* K3: Patch Bank Select */
    {
        uint8_t num_banks = patch_banks_get_num_banks_for_engine(current_engine);
        int val = (int)current_bank + delta;
        if (val < 0) val = 0;
        if (val >= num_banks) val = num_banks - 1;

        if (current_bank != (uint8_t)val) {
            current_bank = (uint8_t)val;
            current_patch = 0; /* Selecting new bank resets patch to first patch */
        }
        patch_change_time = hal_get_ms();
        patch_pending = true;

        ui_sample_set_engine_bank_patch(current_engine, current_bank, current_patch,
                                         patch_banks_get_engine_name(current_engine),
                                         patch_banks_get_bank_name_for_engine(current_engine, current_bank),
                                         patch_banks_get_patch_name_for_engine(current_engine, current_bank, current_patch),
                                         true);
        break;
    }
    case 3: /* K4: Patch Select */
    {
        int num_patches = patch_banks_get_num_patches_for_engine(current_engine, current_bank);
        int val = (int)current_patch + delta;
        if (val < 0) val = 0;
        if (val >= num_patches) val = num_patches - 1;

        current_patch = (uint8_t)val;
        patch_change_time = hal_get_ms();
        patch_pending = true;

        ui_sample_set_engine_bank_patch(current_engine, current_bank, current_patch,
                                         patch_banks_get_engine_name(current_engine),
                                         patch_banks_get_bank_name_for_engine(current_engine, current_bank),
                                         patch_banks_get_patch_name_for_engine(current_engine, current_bank, current_patch),
                                         true);
        break;
    }
    case 4: /* K5: Limiter Threshold T (0.00 to 1.00) */
    {
        current_limiter_t += (float)delta * 0.01f;
        if (current_limiter_t < 0.00f) current_limiter_t = 0.00f;
        if (current_limiter_t > 1.00f) current_limiter_t = 1.00f;
        current_limiter_t = roundf(current_limiter_t * 100.0f) / 100.0f;

        hal_synth_set_limiter(current_limiter_t, current_limiter_m);
        limiter_change_time = hal_get_ms();
        limiter_showing = true;
        ui_sample_show_limiter(current_limiter_t, current_limiter_m, true);
        break;
    }
    case 5: /* K6: Limiter Maximum M (1.0 to 8.0) */
    {
        current_limiter_m += (float)delta * 0.1f;
        if (current_limiter_m < 1.0f) current_limiter_m = 1.0f;
        if (current_limiter_m > 8.0f) current_limiter_m = 8.0f;
        current_limiter_m = roundf(current_limiter_m * 10.0f) / 10.0f;

        hal_synth_set_limiter(current_limiter_t, current_limiter_m);
        limiter_change_time = hal_get_ms();
        limiter_showing = true;
        ui_sample_show_limiter(current_limiter_t, current_limiter_m, false);
        break;
    }
    default:
        break;
    }

    ui_sample_set_encoder(enc_index, delta);
}

static void on_wheel_event(SmkWheel wheel, int16_t value)
{
    if (wheel == SMK_WHEEL_PITCH) {
        hal_synth_pitch_bend(value);
        int32_t b = (int32_t)value + 8192;
        if (b < 0) b = 0;
        if (b > 16383) b = 16383;
        hal_midi_send_pitch_bend(0, (uint16_t)b);
    } else {
        current_mod_wheel = (uint8_t)(value > 127 ? 127 : (value < 0 ? 0 : value));
        sync_shared_params();
        hal_midi_send_cc(0, MIDI_CC_MOD_WHEEL, current_mod_wheel);
    }
    ui_sample_set_wheel(wheel, value);
}

static void on_fader_event(uint8_t fader_index, uint8_t value)
{
    if (fader_index == 2) {
        current_volume = value;
        sync_shared_params();
        hal_audio_set_volume((uint8_t)((current_volume * 100) / 127));
        hal_midi_send_cc(0, MIDI_CC_VOLUME, current_volume);
        vol_change_time = hal_get_ms();
        vol_showing = true;
        ui_sample_show_volume(current_volume);
    } else {
        hal_midi_send_cc(0, (uint8_t)(11 + fader_index), value);
    }
    ui_sample_set_fader(fader_index, value);
}

static void on_button_event(SmkButton button, bool pressed)
{
    if (pressed) {
        if (button == SMK_BTN_OCTAVE_UP) {
            if (current_octave < 3) current_octave++;
            sync_shared_params();
            ui_sample_set_octave(current_octave);
            ui_patch_edit_set_octave(current_octave);
        } else if (button == SMK_BTN_OCTAVE_DOWN) {
            if (current_octave > -3) current_octave--;
            sync_shared_params();
            ui_sample_set_octave(current_octave);
            ui_patch_edit_set_octave(current_octave);
        } else if (button == SMK_BTN_PARA) {
            if (current_engine == SYNTH_ENGINE_MCUNO60) {
                ui_patch_edit_toggle();
            }
        } else if (ui_patch_edit_is_active()) {
            if (button == SMK_BTN_NAV_LEFT) {
                ui_patch_edit_nav_page(-1);
            } else if (button == SMK_BTN_NAV_RIGHT) {
                ui_patch_edit_nav_page(1);
            }
        }
    }

    if (button < SMK_LED_BTN_COUNT) {
        if (button == SMK_BTN_PARA) {
            /* Keep Para button lamp illuminated while parameter editing screen is active */
            hal_led_set_button(SMK_LED_BTN_PARA, ui_patch_edit_is_active());
        } else {
            hal_led_set_button((SmkBtnLed)button, pressed);
        }
    }

    ui_sample_set_button(button, pressed);
}

void app_register_input(void)
{
    /* Queued path: runs on vGuiTask, owns every UI update. */
    HalInputListener listener = {
        .on_key          = on_key_event,
        .on_pad          = on_pad_event,
        .on_pad_pressure = on_pad_pressure_event,
        .on_encoder      = on_encoder_event,
        .on_wheel        = on_wheel_event,
        .on_fader        = on_fader_event,
        .on_button       = on_button_event,
    };
    hal_input_init(&listener);

    /* Low-latency audio path: runs on the scan task at detection time. */
    HalInputAudioListener audio = {
        .on_key_audio          = on_key_audio,
        .on_pad_audio          = on_pad_audio,
        .on_pad_pressure_audio = on_pad_pressure_audio,
    };
    hal_input_init_audio(&audio);
}

void app_init(void)
{
    hal_diag_set_flag(DIAG_FLAG_APP_INIT_ENTRY);

    /* 1. Initialize synthesis engine & MIDI HAL */
    hal_synth_init(AUDIO_DEFAULT_SAMPLE_RATE);
    hal_midi_init();
    hal_diag_set_flag(DIAG_FLAG_SYNTH_INIT_DONE);

    /* 2. Register input callbacks */
    app_register_input();
    hal_diag_set_flag(DIAG_FLAG_INPUT_POST_INIT);

    /* 3. Initialize & start audio DMA stream */
    hal_diag_set_square(2, 14, DIAG_COLOR_YELLOW); /* Row 2 Col 14: hal_audio_init entered */
    hal_audio_init(AUDIO_DEFAULT_SAMPLE_RATE, hal_synth_get_samples);
    hal_audio_start();
    hal_diag_set_square(2, 14, DIAG_COLOR_GREEN);  /* Row 2 Col 14: hal_audio_init returned */
    hal_diag_set_flag(DIAG_FLAG_HAL_AUDIO_INIT_DONE);
    hal_diag_set_flag(DIAG_FLAG_HAL_AUDIO_START_DONE);

    /* Sync initial synth parameters & master audio volume */
    sync_shared_params();
    hal_audio_set_volume((uint8_t)((current_volume * 100) / 127));
    hal_synth_set_engine(current_engine);
    hal_synth_set_bank_patch(current_bank, current_patch);
    hal_synth_set_limiter(current_limiter_t, current_limiter_m);

    /* 4. Build LVGL sample screen & patch edit screen */
    hal_diag_set_square(2, 15, DIAG_COLOR_CYAN);   /* Row 2 Col 15: ui_sample_init entered */
    ui_sample_init();
    ui_patch_edit_init();
    if (current_engine == SYNTH_ENGINE_MCUNO60) {
        ui_patch_edit_load_patch(current_bank, current_patch);
    }
    ui_patch_edit_set_octave(current_octave);
    hal_diag_set_square(2, 15, DIAG_COLOR_GREEN);  /* Row 2 Col 15: ui_sample_init returned */
    ui_sample_set_engine_bank_patch(current_engine, current_bank, current_patch,
                                     patch_banks_get_engine_name(current_engine),
                                     patch_banks_get_bank_name_for_engine(current_engine, current_bank),
                                     patch_banks_get_patch_name_for_engine(current_engine, current_bank, current_patch),
                                     false);
    hal_diag_set_flag(DIAG_FLAG_UI_SAMPLE_INIT_DONE);

    /* 5. Initialize LED subsystem */
    hal_led_init();

    /* 6. Begin brief startup animation sequence prior to synth UI */
    startup_anim_init();
}

void app_step(void)
{
    hal_input_poll();

    if (startup_anim_is_running()) {
        if (!startup_anim_step()) {
            startup_anim_finish();
        }
        return;
    }

    uint32_t now = hal_get_ms();

    /* Apply pending engine/bank/patch change 0.5s after last adjustment */
    if (patch_pending && (now - patch_change_time >= 500)) {
        patch_pending = false;
        hal_synth_set_engine(current_engine);
        hal_synth_set_bank_patch(current_bank, current_patch);
        if (current_engine == SYNTH_ENGINE_MCUNO60) {
            ui_patch_edit_load_patch(current_bank, current_patch);
        }
        ui_sample_set_engine_bank_patch(current_engine, current_bank, current_patch,
                                         patch_banks_get_engine_name(current_engine),
                                         patch_banks_get_bank_name_for_engine(current_engine, current_bank),
                                         patch_banks_get_patch_name_for_engine(current_engine, current_bank, current_patch),
                                         false);
    }

    /* Hide volume display overlay 0.5s after last adjustment */
    if (vol_showing && (now - vol_change_time >= 500)) {
        vol_showing = false;
        ui_sample_clear_volume_overlay();
    }

    /* Hide limiter display overlay 1.5s after last adjustment */
    if (limiter_showing && (now - limiter_change_time >= 1500)) {
        limiter_showing = false;
        ui_sample_clear_volume_overlay();
    }

    ui_sample_update();
    ui_patch_edit_update();

    /* Flush staged pad RGB updates to physical WS2812B LEDs */
    hal_led_update_pads();
}
