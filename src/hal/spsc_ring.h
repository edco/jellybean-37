/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file spsc_ring.h
 * Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer
 * Designed for cross-core messaging (Core 0 -> Core 1) on dual-core architectures
 *
 * CONCENTRATOR ARCHITECTURE:
 * Core 0 producers (keybed scan, USB MIDI ISR, GUI) post events to a FreeRTOS
 * queue. A high-priority bridge task (vEventBridgeTask) drains that queue and
 * serves as the SOLE producer to core_event_ring. Core 1 is the sole consumer.
 *
 * All real-time events (note on/off, pitch bend, CC, program change, engine select)
 * travel in time order through this compact 4-byte event stream. Shared memory is
 * retained solely for Core 1 -> Core 0 telemetry (SynthSharedFeedback).
 */

#ifndef SPSC_RING_H
#define SPSC_RING_H

#include <stdint.h>
#include <stdbool.h>
#ifndef BOOL_DEFINE_CONFLICT
#define BOOL_DEFINE_CONFLICT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__PI32V2__)
#define SPSC_MEMORY_BARRIER()           __asm__ volatile("ssync; csync" ::: "memory")
#else
#define SPSC_MEMORY_BARRIER()           __asm__ volatile("" ::: "memory")
#endif

/* Cross-core real-time event types */
typedef enum {
    SPSC_EVT_NONE = 0,
    SPSC_EVT_NOTE_ON,
    SPSC_EVT_NOTE_OFF,
    SPSC_EVT_CC,
    SPSC_EVT_PITCH_BEND,
    SPSC_EVT_ALL_NOTES_OFF,
    SPSC_EVT_PROGRAM_CHANGE,
    SPSC_EVT_AFTERTOUCH,
    SPSC_EVT_ENGINE_SELECT,
    SPSC_EVT_PARAM_CHANGE
} SpscEventType;

/* Backwards compatibility aliases */
#define SPSC_EVT_CONTROL_CHANGE SPSC_EVT_CC

/* Compact 4-byte native-word event union for uncached internal SRAM */
typedef union {
    struct {
        uint8_t  type;       /* Event type (SpscEventType) */
        uint8_t  channel;    /* MIDI channel 0..15 */
        union {
            struct {
                uint8_t data1;  /* Note number, CC ID, or Bank index */
                uint8_t data2;  /* Velocity, CC value, or Patch index */
            };
            int16_t  svalue16;  /* 14-bit signed Pitch Bend (-8192..+8191) */
            uint16_t uvalue16;  /* 16-bit unsigned parameter / NRPN value */
        };
    };
    uint32_t raw;          /* Native 32-bit atomic word access */
} SpscEvent;

#if defined(__cplusplus)
static_assert(sizeof(SpscEvent) == 4, "SpscEvent must be exactly 4 bytes");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(SpscEvent) == 4, "SpscEvent must be exactly 4 bytes");
#endif

#define SPSC_RING_SIZE 64  /* 64 * 4B = 256B total (reduced from 2048B) */
#define SPSC_RING_MASK (SPSC_RING_SIZE - 1)

typedef struct {
    SpscEvent buffer[SPSC_RING_SIZE]; /* 256 bytes */

    /* Producer (Core 0) control */
    volatile uint32_t head;

    /* Consumer (Core 1) control */
    volatile uint32_t tail;
} SpscRing;

static inline void spsc_ring_init(SpscRing *ring)
{
    ring->head = 0;
    ring->tail = 0;
    SPSC_MEMORY_BARRIER();
}

static inline bool spsc_ring_push(SpscRing *ring, const SpscEvent *event)
{
    uint32_t head = ring->head;
    uint32_t next_head = (head + 1) & SPSC_RING_MASK;
    if (next_head == ring->tail) {
        return false; /* Ring full */
    }

    ring->buffer[head] = *event;
    SPSC_MEMORY_BARRIER();

    ring->head = next_head;
    SPSC_MEMORY_BARRIER();
    return true;
}

static inline bool spsc_ring_pop(SpscRing *ring, SpscEvent *event)
{
    uint32_t tail = ring->tail;
    if (tail == ring->head) {
        return false; /* Ring empty */
    }

    *event = ring->buffer[tail];
    SPSC_MEMORY_BARRIER();

    ring->tail = (tail + 1) & SPSC_RING_MASK;
    SPSC_MEMORY_BARRIER();
    return true;
}

/* Global cross-core event ring buffer */
extern SpscRing core_event_ring;

/* =========================================================================
 * Reverse Telemetry Feedback Block (Core 1 -> Core 0)
 * ========================================================================= */
typedef struct {
    volatile uint8_t vu_level;      /* Audio VU peak level (0..100) */
    volatile uint8_t cpu_load_pct;  /* Core 1 DSP load (0..100) */
    volatile uint8_t active_voices; /* Number of voices currently active */
    uint8_t          reserved;
} SynthSharedFeedback;

extern SynthSharedFeedback synth_feedback;

static inline void synth_feedback_init(SynthSharedFeedback *fb)
{
    fb->vu_level = 0;
    fb->cpu_load_pct = 0;
    fb->active_voices = 0;
    fb->reserved = 0;
    SPSC_MEMORY_BARRIER();
}

static inline void synth_feedback_write(SynthSharedFeedback *fb, uint8_t vu)
{
    fb->vu_level = vu;
    fb->cpu_load_pct = vu;
    SPSC_MEMORY_BARRIER();
}

static inline uint8_t synth_feedback_read(SynthSharedFeedback *fb)
{
    SPSC_MEMORY_BARRIER();
    return fb->vu_level;
}

#ifdef __cplusplus
}
#endif

#endif /* SPSC_RING_H */
