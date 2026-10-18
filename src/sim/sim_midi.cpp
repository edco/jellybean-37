/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file sim_midi.cpp
 * Native Linux Simulator Backend for HAL MIDI & USB Audio
 */

#include "hal_midi.h"
#include <cstdio>

extern "C" {

int hal_midi_init(void)
{
    return 0;
}

void hal_midi_send_note_on(uint8_t ch, uint8_t note, uint8_t velocity)
{
    (void)ch; (void)note; (void)velocity;
}

void hal_midi_send_note_off(uint8_t ch, uint8_t note, uint8_t velocity)
{
    (void)ch; (void)note; (void)velocity;
}

void hal_midi_send_cc(uint8_t ch, uint8_t cc, uint8_t value)
{
    (void)ch; (void)cc; (void)value;
}

void hal_midi_send_pitch_bend(uint8_t ch, uint16_t bend_14bit)
{
    (void)ch; (void)bend_14bit;
}

void hal_midi_send_channel_pressure(uint8_t ch, uint8_t pressure)
{
    (void)ch; (void)pressure;
}

void hal_midi_send_poly_pressure(uint8_t ch, uint8_t note, uint8_t pressure)
{
    (void)ch; (void)note; (void)pressure;
}

void hal_midi_send_program_change(uint8_t ch, uint8_t program)
{
    (void)ch; (void)program;
}

void hal_midi_send_raw(const uint8_t *data, size_t len)
{
    (void)data; (void)len;
}

bool hal_usb_is_configured(void)
{
    return true;
}

bool hal_usb_audio_is_active(void)
{
    return false;
}

void hal_usb_audio_push_samples(const int16_t *pcm16, size_t frames)
{
    (void)pcm16; (void)frames;
}

} // extern "C"
