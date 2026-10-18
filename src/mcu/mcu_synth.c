/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_synth.c
 * MCU Target Synthesizer HAL & Cross-Core IPC implementation
 */

#include "hal_synth.h"
#include "spsc_ring.h"
#include "asm/cpu.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Global IPC objects */
SpscRing core_event_ring __attribute__((aligned(32)));
SynthSharedFeedback synth_feedback __attribute__((aligned(32)));

static QueueHandle_t core0_event_queue = NULL;
static TaskHandle_t  bridge_task_handle = NULL;
static bool synth_initialized = false;

/* Static FreeRTOS storage for concentrator queue and bridge task */
static StaticQueue_t bridge_queue_struct;
static uint8_t bridge_queue_storage[64 * sizeof(SpscEvent)];
static StaticTask_t bridge_task_tcb;
static StackType_t bridge_task_stack[512];

static void vEventBridgeTask(void *pvParameters)
{
    (void)pvParameters;
    SpscEvent evt;
    while (1) {
        if (xQueueReceive(core0_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            spsc_ring_push(&core_event_ring, &evt);
        }
    }
}

void hal_synth_init(uint32_t sample_rate)
{
    (void)sample_rate;
    if (synth_initialized) {
        return;
    }
    synth_initialized = true;

    spsc_ring_init(&core_event_ring);
    synth_feedback_init(&synth_feedback);

    if (core0_event_queue == NULL) {
        core0_event_queue = xQueueCreateStatic(
            64,
            sizeof(SpscEvent),
            bridge_queue_storage,
            &bridge_queue_struct
        );
    }

    if (bridge_task_handle == NULL) {
        bridge_task_handle = xTaskCreateStatic(
            vEventBridgeTask,
            "#C0evt_bridge",
            512,
            NULL,
            9,
            bridge_task_stack,
            &bridge_task_tcb
        );
    }
}

static inline void send_event(const SpscEvent *evt)
{
    if (core0_event_queue != NULL) {
        xQueueSend(core0_event_queue, evt, 0);
    }
}

void hal_synth_set_engine(uint8_t engine)
{
    SpscEvent evt;
    evt.raw = 0;
    evt.type = SPSC_EVT_ENGINE_SELECT;
    evt.channel = 0;
    evt.data1 = engine;
    send_event(&evt);
}

void hal_synth_set_bank_patch(uint8_t bank, uint8_t patch)
{
    SpscEvent evt;
    evt.raw = 0;
    evt.type = SPSC_EVT_PROGRAM_CHANGE;
    evt.channel = 0;
    evt.data1 = bank;
    evt.data2 = patch;
    send_event(&evt);
}

void hal_synth_set_param(uint8_t param_id, float value)
{
    SpscEvent evt;
    evt.raw = 0;
    evt.type = SPSC_EVT_PARAM_CHANGE;
    evt.channel = param_id;
    if (param_id == 0 || param_id == 32) {
        evt.uvalue16 = (uint16_t)(value * 100.0f + 0.5f);
    } else if (param_id == 1) {
        evt.uvalue16 = (uint16_t)(value * 1000.0f + 0.5f);
    } else if (param_id == 15) {
        evt.svalue16 = (value < 0.0f) ? -1 : 1;
    } else if (param_id == 4 || param_id == 11 || param_id == 18 || param_id == 24 || param_id == 31 || param_id == 33 || param_id == 34) {
        evt.uvalue16 = (uint16_t)value;
    } else {
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;
        evt.uvalue16 = (uint16_t)(value * 10000.0f + 0.5f);
    }
    send_event(&evt);
}

void hal_synth_note_on(uint8_t note, uint8_t velocity)
{
    SpscEvent evt;
    evt.raw = 0;
    evt.type = SPSC_EVT_NOTE_ON;
    evt.channel = 0;
    evt.data1 = note;
    evt.data2 = velocity ? velocity : 100;
    send_event(&evt);
}

void hal_synth_note_off(uint8_t note)
{
    SpscEvent evt;
    evt.raw = 0;
    evt.type = SPSC_EVT_NOTE_OFF;
    evt.channel = 0;
    evt.data1 = note;
    evt.data2 = 64;
    send_event(&evt);
}

void hal_synth_all_notes_off(void)
{
    SpscEvent evt;
    evt.raw = 0;
    evt.type = SPSC_EVT_ALL_NOTES_OFF;
    send_event(&evt);
}

void hal_synth_pitch_bend(int16_t bend)
{
    SpscEvent evt;
    evt.raw = 0;
    evt.type = SPSC_EVT_PITCH_BEND;
    evt.channel = 0;
    evt.svalue16 = bend;
    send_event(&evt);
}

void hal_synth_mod_wheel(uint8_t val)
{
    SpscEvent evt;
    evt.raw = 0;
    evt.type = SPSC_EVT_CC;
    evt.channel = 0;
    evt.data1 = 1; /* CC 1: Mod Wheel */
    evt.data2 = val;
    send_event(&evt);
}

void hal_synth_aftertouch(uint8_t pressure)
{
    SpscEvent evt;
    evt.raw = 0;
    evt.type = SPSC_EVT_AFTERTOUCH;
    evt.channel = 0;
    evt.data1 = pressure;
    send_event(&evt);
}

void hal_synth_set_volume(uint8_t volume)
{
    SpscEvent evt;
    evt.raw = 0;
    evt.type = SPSC_EVT_CC;
    evt.channel = 0;
    evt.data1 = 7; /* CC 7: Channel Volume */
    evt.data2 = volume;
    send_event(&evt);
}

void hal_synth_set_octave(int8_t octave)
{
    (void)octave;
    /* Keyboard octave offset is applied directly to midi note in on_key_audio */
}

void hal_synth_midi_event_from_isr(uint8_t type, uint8_t channel, uint8_t d1, uint8_t d2)
{
    if (core0_event_queue == NULL) return;

    SpscEvent evt;
    evt.raw = 0;
    evt.type = type;
    evt.channel = channel;
    evt.data1 = d1;
    evt.data2 = d2;

    xQueueSendFromISR(core0_event_queue, &evt, NULL);
}

uint8_t hal_synth_get_vu_level(void)
{
    return synth_feedback_read(&synth_feedback);
}

void hal_synth_get_samples(int16_t *buffer, size_t frames)
{
    (void)buffer;
    (void)frames;
    /* On MCU, Core 1 audio DSP loop in cpu1_entry calls synth_get_samples directly */
}
