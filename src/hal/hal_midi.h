/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file hal_midi.h
 * Hardware Abstraction Layer for USB MIDI and Audio Streaming
 */

#ifndef HAL_MIDI_H
#define HAL_MIDI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard MIDI status definitions */
#define MIDI_STATUS_NOTE_OFF            0x80
#define MIDI_STATUS_NOTE_ON             0x90
#define MIDI_STATUS_POLY_KEY_PRESSURE   0xA0
#define MIDI_STATUS_CONTROL_CHANGE      0xB0
#define MIDI_STATUS_PROGRAM_CHANGE      0xC0
#define MIDI_STATUS_CHANNEL_PRESSURE    0xD0
#define MIDI_STATUS_PITCH_BEND          0xE0
#define MIDI_STATUS_SYSEX_START         0xF0
#define MIDI_STATUS_SYSEX_END           0xF7

/* Common Control Change definitions */
#define MIDI_CC_MOD_WHEEL               1
#define MIDI_CC_VOLUME                  7
#define MIDI_CC_PAN                     10
#define MIDI_CC_EXPRESSION              11
#define MIDI_CC_SUSTAIN                 64
#define MIDI_CC_ALL_SOUND_OFF           120
#define MIDI_CC_ALL_CONTROLLERS_OFF     121
#define MIDI_CC_ALL_NOTES_OFF           123

/**
 * Initialize MIDI HAL subsystem.
 */
int hal_midi_init(void);

/**
 * Transmit MIDI Note On.
 * @param ch Channel (0..15)
 * @param note Note number (0..127)
 * @param velocity Velocity (1..127; 0 is interpreted as note off)
 */
void hal_midi_send_note_on(uint8_t ch, uint8_t note, uint8_t velocity);

/**
 * Transmit MIDI Note Off.
 * @param ch Channel (0..15)
 * @param note Note number (0..127)
 * @param velocity Release velocity (typically 0 or 64)
 */
void hal_midi_send_note_off(uint8_t ch, uint8_t note, uint8_t velocity);

/**
 * Transmit MIDI Control Change.
 * @param ch Channel (0..15)
 * @param cc Controller number (0..127)
 * @param value Controller value (0..127)
 */
void hal_midi_send_cc(uint8_t ch, uint8_t cc, uint8_t value);

/**
 * Transmit Pitch Bend.
 * @param ch Channel (0..15)
 * @param bend_14bit 14-bit unsigned pitch bend (0..16383, 8192 = center)
 */
void hal_midi_send_pitch_bend(uint8_t ch, uint16_t bend_14bit);

/**
 * Transmit MIDI Channel Pressure (Aftertouch).
 * @param ch Channel (0..15)
 * @param pressure Pressure level (0..127)
 */
void hal_midi_send_channel_pressure(uint8_t ch, uint8_t pressure);

/**
 * Transmit MIDI Polyphonic Key Pressure.
 * @param ch Channel (0..15)
 * @param note Note number (0..127)
 * @param pressure Pressure level (0..127)
 */
void hal_midi_send_poly_pressure(uint8_t ch, uint8_t note, uint8_t pressure);

/**
 * Transmit Program Change.
 * @param ch Channel (0..15)
 * @param program Program number (0..127)
 */
void hal_midi_send_program_change(uint8_t ch, uint8_t program);

/**
 * Transmit raw SysEx or custom MIDI packet buffer.
 */
void hal_midi_send_raw(const uint8_t *data, size_t len);

/**
 * Query whether USB device connection is active and configured.
 */
bool hal_usb_is_configured(void);

/**
 * Query whether USB Audio streaming (host recording) is active.
 */
bool hal_usb_audio_is_active(void);

/**
 * Push rendered stereo PCM samples to USB Audio streaming endpoint.
 * Called from real-time audio thread.
 * @param pcm16 Interleaved 16-bit signed stereo samples [L0, R0, L1, R1, ...]
 * @param frames Number of stereo frames (sample pairs)
 */
void hal_usb_audio_push_samples(const int16_t *pcm16, size_t frames);

#ifdef __cplusplus
}
#endif

#endif /* HAL_MIDI_H */
