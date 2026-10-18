/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file hal_synth.h
 * Hardware Abstraction Layer for Synthesizer Engine & Cross-Core IPC
 */

#ifndef HAL_SYNTH_H
#define HAL_SYNTH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize synthesizer subsystem.
 * On NATIVE, initializes Dexed audio engine.
 * On MCU, sets up shared IPC buffers; Core 1 initializes Dexed directly.
 */
void hal_synth_init(uint32_t sample_rate);

/**
 * Select active engine (0: Dexed, 1: MCUno-60).
 */
void hal_synth_set_engine(uint8_t engine);

/**
 * Trigger Note-On.
 * @param note MIDI note number (0..127)
 * @param velocity Note velocity (1..127)
 */
void hal_synth_note_on(uint8_t note, uint8_t velocity);

/**
 * Trigger Note-Off.
 * @param note MIDI note number (0..127)
 */
void hal_synth_note_off(uint8_t note);

/**
 * Silence all active voices.
 */
void hal_synth_all_notes_off(void);

/**
 * Set pitch bend.
 * @param bend Pitch bend value (-8192..+8191, center 0)
 */
void hal_synth_pitch_bend(int16_t bend);

/**
 * Set modulation wheel depth.
 * @param val Modulation depth (0..127)
 */
void hal_synth_mod_wheel(uint8_t val);

/**
 * Set aftertouch / channel pressure depth.
 * @param pressure Pressure level (0..127)
 */
void hal_synth_aftertouch(uint8_t pressure);

/**
 * Set master output volume.
 * @param volume Volume level (0..127)
 */
void hal_synth_set_volume(uint8_t volume);

/**
 * Set current active keyboard octave offset.
 * @param octave Octave offset (-3..+3)
 */
void hal_synth_set_octave(int8_t octave);

/**
 * Select active bank and patch.
 * @param bank Bank index (0..N-1)
 * @param patch Patch index (0..N-1)
 */
void hal_synth_set_bank_patch(uint8_t bank, uint8_t patch);

/**
 * Core 0 ISR-safe entry point for external MIDI ingress.
 * @param type SpscEventType
 * @param channel MIDI channel (0..15)
 * @param d1 Data byte 1
 * @param d2 Data byte 2
 */
void hal_synth_midi_event_from_isr(uint8_t type, uint8_t channel, uint8_t d1, uint8_t d2);

/**
 * Retrieve peak audio level for UI VU meter.
 * @return VU level (0..100)
 */
uint8_t hal_synth_get_vu_level(void);

/**
 * Render interleaved 16-bit signed stereo PCM audio samples.
 * @param buffer Output buffer of size (frames * 2 * sizeof(int16_t))
 * @param frames Number of stereo frames to generate
 */
void hal_synth_get_samples(int16_t *buffer, size_t frames);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SYNTH_H */
