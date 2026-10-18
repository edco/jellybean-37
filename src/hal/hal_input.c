/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file hal_input.c
 * Input dispatcher implementation
 */

#include "hal_input.h"
#include <string.h>

static HalInputListener input_listener = {0};
static HalInputAudioListener audio_listener = {0};

void hal_input_init(const HalInputListener *listener)
{
    if (listener) {
        input_listener = *listener;
    } else {
        memset(&input_listener, 0, sizeof(input_listener));
    }
}

void hal_input_init_audio(const HalInputAudioListener *listener)
{
    if (listener) {
        audio_listener = *listener;
    } else {
        memset(&audio_listener, 0, sizeof(audio_listener));
    }
}

typedef enum {
    EVT_KEY = 1,
    EVT_PAD,
    EVT_PAD_PRESSURE,
    EVT_BUTTON,
    EVT_ENCODER,
    EVT_WHEEL,
    EVT_FADER
} InputEvtType;

typedef struct {
    uint8_t type;
    uint8_t id;
    uint8_t val8;
    int16_t val16;
} InputQueuedEvt;

#define INPUT_QUEUE_SIZE 64
static InputQueuedEvt evt_queue[INPUT_QUEUE_SIZE];
static volatile uint8_t evt_head = 0;
static volatile uint8_t evt_tail = 0;

/* Lock-free Single-Producer (vKeybedScanTask) Single-Consumer (vGuiTask) FIFO */
static inline void queue_input_event(uint8_t type, uint8_t id, uint8_t v8, int16_t v16)
{
    uint8_t head = evt_head;
    uint8_t next = (head + 1) % INPUT_QUEUE_SIZE;
    if (next != evt_tail) {
        evt_queue[head].type = type;
        evt_queue[head].id = id;
        evt_queue[head].val8 = v8;
        evt_queue[head].val16 = v16;
#if defined(__PI32V2__)
        __asm__ volatile("csync" ::: "memory");
#else
        __asm__ volatile("" ::: "memory");
#endif
        evt_head = next;
    }
}

void hal_input_dispatch_key(uint8_t key_index, bool pressed, uint8_t velocity)
{
    if (key_index < SMK_NUM_KEYS) {
        /* Audio first, and on this task, at the moment of detection. The
         * queued copy below is for the GUI and will only reach the synth a
         * GUI wake later. */
        if (audio_listener.on_key_audio) {
            audio_listener.on_key_audio(key_index, pressed, velocity);
        }

        queue_input_event(EVT_KEY, key_index, pressed ? 1 : 0, velocity);
    }
}

void hal_input_dispatch_pad(uint8_t pad_index, bool pressed, uint8_t velocity)
{
    if (pad_index < SMK_NUM_PADS) {
        /* Same low-latency audio path as keys: dispatched on the scan task at
         * the moment of detection. */
        if (audio_listener.on_pad_audio) {
            audio_listener.on_pad_audio(pad_index, pressed, velocity);
        }
        queue_input_event(EVT_PAD, pad_index, pressed ? 1 : 0, velocity);
    }
}

void hal_input_dispatch_pad_pressure(uint8_t pad_index, uint8_t pressure)
{
    if (pad_index < SMK_NUM_PADS) {
        if (audio_listener.on_pad_pressure_audio) {
            audio_listener.on_pad_pressure_audio(pad_index, pressure);
        }
        queue_input_event(EVT_PAD_PRESSURE, pad_index, pressure, 0);
    }
}

void hal_input_dispatch_encoder(uint8_t enc_index, int8_t delta)
{
    if (enc_index < SMK_NUM_ENCODERS) {
        queue_input_event(EVT_ENCODER, enc_index, 0, delta);
    }
}

void hal_input_dispatch_wheel(SmkWheel wheel, int16_t value)
{
    queue_input_event(EVT_WHEEL, (uint8_t)wheel, 0, value);
}

void hal_input_dispatch_fader(uint8_t fader_index, uint8_t value)
{
    if (fader_index < SMK_NUM_FADERS) {
        queue_input_event(EVT_FADER, fader_index, value, 0);
    }
}

void hal_input_dispatch_button(SmkButton button, bool pressed)
{
    if (button < SMK_BTN_COUNT) {
        queue_input_event(EVT_BUTTON, (uint8_t)button, pressed ? 1 : 0, 0);
    }
}

void hal_input_poll(void)
{
    while (evt_tail != evt_head) {
        uint8_t tail = evt_tail;
        InputQueuedEvt evt = evt_queue[tail];
#if defined(__PI32V2__)
        __asm__ volatile("csync" ::: "memory");
#else
        __asm__ volatile("" ::: "memory");
#endif
        evt_tail = (tail + 1) % INPUT_QUEUE_SIZE;

        switch (evt.type) {
        case EVT_KEY:
            if (input_listener.on_key) {
                input_listener.on_key(evt.id, evt.val8 != 0, (uint8_t)evt.val16);
            }
            break;
        case EVT_PAD:
            if (input_listener.on_pad) {
                input_listener.on_pad(evt.id, evt.val8 != 0, (uint8_t)evt.val16);
            }
            break;
        case EVT_PAD_PRESSURE:
            if (input_listener.on_pad_pressure) {
                input_listener.on_pad_pressure(evt.id, evt.val8);
            }
            break;
        case EVT_BUTTON:
            if (input_listener.on_button) {
                input_listener.on_button((SmkButton)evt.id, evt.val8 != 0);
            }
            break;
        case EVT_ENCODER:
            if (input_listener.on_encoder) {
                input_listener.on_encoder(evt.id, (int8_t)evt.val16);
            }
            break;
        case EVT_WHEEL:
            if (input_listener.on_wheel) {
                input_listener.on_wheel((SmkWheel)evt.id, evt.val16);
            }
            break;
        case EVT_FADER:
            if (input_listener.on_fader) {
                input_listener.on_fader(evt.id, evt.val8);
            }
            break;
        }
    }
}

void hal_input_notify_matrix_delta(uint32_t c) { (void)c; }
void hal_input_notify_matrix_raw(const uint8_t *b, uint32_t s) { (void)b; (void)s; }
void hal_input_notify_matrix_full(const uint8_t *r, const uint8_t *e, uint32_t s) { (void)r; (void)e; (void)s; }
void hal_input_notify_irq_stats(uint8_t m, uint32_t s, uint32_t a, uint32_t f) { (void)m; (void)s; (void)a; (void)f; }
void hal_input_notify_spi2_iomap(uint32_t i) { (void)i; }
void hal_input_notify_bank_pads(uint32_t b) { (void)b; }
void hal_input_notify_scan(void) {}
