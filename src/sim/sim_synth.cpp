/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file sim_synth.cpp
 * Native Simulation HAL implementation for Synthesizer Engine
 */

#include "hal_synth.h"
#include "spsc_ring.h"
#include "synth_engine.h"

extern "C" {

void hal_synth_init(uint32_t sample_rate)
{
    synth_init(sample_rate);
}

void hal_synth_set_engine(uint8_t engine)
{
    synth_set_engine(engine);
}

void hal_synth_note_on(uint8_t note, uint8_t velocity)
{
    synth_note_on(note, velocity);
}

void hal_synth_note_off(uint8_t note)
{
    synth_note_off(note);
}

void hal_synth_all_notes_off(void)
{
    synth_all_notes_off();
}

void hal_synth_pitch_bend(int16_t bend)
{
    synth_set_pitch_bend(bend);
}

void hal_synth_mod_wheel(uint8_t val)
{
    synth_set_mod_wheel(val);
}

void hal_synth_aftertouch(uint8_t pressure)
{
    synth_set_aftertouch(pressure);
}

void hal_synth_set_volume(uint8_t volume)
{
    synth_set_volume(volume);
}

void hal_synth_set_octave(int8_t octave)
{
    (void)octave;
    /* On native, octave offset is applied directly to midi note in on_key_event */
}

void hal_synth_set_bank_patch(uint8_t bank, uint8_t patch)
{
    synth_set_bank_patch(bank, patch);
}

uint8_t hal_synth_get_vu_level(void)
{
    return synth_get_vu_level();
}

void hal_synth_midi_event_from_isr(uint8_t type, uint8_t channel, uint8_t d1, uint8_t d2)
{
    (void)channel;
    switch (type) {
    case SPSC_EVT_NOTE_ON:
        synth_note_on(d1, d2);
        break;
    case SPSC_EVT_NOTE_OFF:
        synth_note_off(d1);
        break;
    case SPSC_EVT_ALL_NOTES_OFF:
        synth_all_notes_off();
        break;
    case SPSC_EVT_PITCH_BEND: {
        int16_t bend = (int16_t)((uint16_t)d1 | ((uint16_t)d2 << 8));
        synth_set_pitch_bend(bend);
        break;
    }
    case SPSC_EVT_CC:
        if (d1 == 1) {
            synth_set_mod_wheel(d2);
        } else if (d1 == 7) {
            synth_set_volume(d2);
        }
        break;
    case SPSC_EVT_AFTERTOUCH:
        synth_set_aftertouch(d1);
        break;
    case SPSC_EVT_PROGRAM_CHANGE:
        synth_set_bank_patch(d1, d2);
        break;
    case SPSC_EVT_ENGINE_SELECT:
        synth_set_engine(d1);
        break;
    default:
        break;
    }
}

void hal_synth_get_samples(int16_t *buffer, size_t frames)
{
    synth_get_samples(buffer, frames);
}

} // extern "C"
