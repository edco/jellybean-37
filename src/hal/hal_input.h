/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file hal_input.h
 * Hardware Abstraction Layer for SMK-37 Pro Physical & Simulated Controls:
 * - 37-key chromatic keybed
 * - 16 velocity-sensitive drum pads
 * - 8 endless rotary encoders (relative step up/down)
 * - Pitch bend & modulation wheels
 * - 4 analog faders (0..127)
 * - 18 dedicated function / navigation buttons
 */

#ifndef HAL_INPUT_H
#define HAL_INPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMK_NUM_KEYS        37
#define SMK_NUM_PADS        16
#define SMK_NUM_ENCODERS    8
#define SMK_NUM_FADERS      4

typedef enum {
    SMK_BTN_KNOB_BANK = 0,
    SMK_BTN_FADER_BANK,
    SMK_BTN_NAV_LEFT,
    SMK_BTN_NAV_RIGHT,
    SMK_BTN_OCTAVE_DOWN,
    SMK_BTN_OCTAVE_UP,
    SMK_BTN_ARP,
    SMK_BTN_NOTE_REPEAT,
    SMK_BTN_SCALE,
    SMK_BTN_CHORD,
    SMK_BTN_GLOBE,
    SMK_BTN_BT,
    SMK_BTN_PATCH,
    SMK_BTN_PARA,
    SMK_BTN_FX,
    SMK_BTN_SEQ,
    SMK_BTN_SEQ_PLAY,
    SMK_BTN_SEQ_REC,
    SMK_BTN_COUNT
} SmkButton;

typedef enum {
    SMK_WHEEL_PITCH = 0,
    SMK_WHEEL_MOD
} SmkWheel;

/* Event listener callbacks */
typedef void (*HalKeyCallback)(uint8_t key_index, bool pressed, uint8_t velocity);
typedef void (*HalPadCallback)(uint8_t pad_index, bool pressed, uint8_t velocity);
typedef void (*HalPadPressureCallback)(uint8_t pad_index, uint8_t pressure);
typedef void (*HalEncoderCallback)(uint8_t enc_index, int8_t delta);
typedef void (*HalWheelCallback)(SmkWheel wheel, int16_t value);
typedef void (*HalFaderCallback)(uint8_t fader_index, uint8_t value);
typedef void (*HalButtonCallback)(SmkButton button, bool pressed);

typedef struct {
    HalKeyCallback          on_key;
    HalPadCallback          on_pad;
    HalPadPressureCallback on_pad_pressure;
    HalEncoderCallback      on_encoder;
    HalWheelCallback        on_wheel;
    HalFaderCallback        on_fader;
    HalButtonCallback       on_button;
} HalInputListener;

/**
 * Low-latency audio listener.
 *
 * These callbacks run SYNCHRONOUSLY inside hal_input_dispatch_key() /
 * hal_input_dispatch_pad(), i.e. on the scan task at the exact moment a
 * contact edge is detected, before the event is queued for the GUI. That is
 * what keeps note-on latency at the scan interval rather than at the GUI
 * task's wake period.
 *
 * They therefore run OUTSIDE the queued path and must obey two rules:
 *   1. Touch no UI. Anything reaching LVGL from this context is a
 *      cross-task violation, since only vGuiTask may touch it.
 *   2. Touch only state owned by the scan task. They run on the same task
 *      that produced the event, so there is no sharing race with the
 *      queued handler -- but the queued handler must not then re-issue the
 *      same audio call, or notes would be triggered twice.
 */
typedef struct {
    HalKeyCallback          on_key_audio;
    HalPadCallback          on_pad_audio;
    HalPadPressureCallback on_pad_pressure_audio;
} HalInputAudioListener;

/**
 * Initialize input subsystem and register listener callbacks.
 */
void hal_input_init(const HalInputListener *listener);

/**
 * Register the low-latency audio listener. Must be called before the scan
 * task starts dispatching. Optional: a NULL listener simply means audio
 * events are only delivered through the queued path.
 */
void hal_input_init_audio(const HalInputAudioListener *listener);

/**
 * Dispatch events from driver / simulator into the registered listener.
 */
void hal_input_dispatch_key(uint8_t key_index, bool pressed, uint8_t velocity);
void hal_input_dispatch_pad(uint8_t pad_index, bool pressed, uint8_t velocity);
void hal_input_dispatch_pad_pressure(uint8_t pad_index, uint8_t pressure);
void hal_input_dispatch_encoder(uint8_t enc_index, int8_t delta);
void hal_input_dispatch_wheel(SmkWheel wheel, int16_t value);
void hal_input_dispatch_fader(uint8_t fader_index, uint8_t value);
void hal_input_dispatch_button(SmkButton button, bool pressed);

/**
 * Polling or hardware scan hook called periodically.
 */
void hal_input_poll(void);

void hal_input_notify_matrix_delta(uint32_t c);
void hal_input_notify_matrix_raw(const uint8_t *b, uint32_t s);
void hal_input_notify_matrix_full(const uint8_t *r, const uint8_t *e, uint32_t s);
void hal_input_notify_irq_stats(uint8_t m, uint32_t s, uint32_t a, uint32_t f);
void hal_input_notify_spi2_iomap(uint32_t i);
void hal_input_notify_bank_pads(uint32_t b);
void hal_input_notify_scan(void);



#ifdef __cplusplus
}
#endif

#endif /* HAL_INPUT_H */
