/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file dexed_shim.cpp
 * Dexed FM Synthesis Engine Shim Implementation
 */

#include "dexed_shim.h"
#include "patch_banks.h"
#include <string.h>

#ifndef PROGMEM
#define PROGMEM
#endif
#include "banks.h"

static_assert(LG_N == 5, "LG_N must be 5 (32 sample blocks)");
static_assert(_N_ == 32, "_N_ must be 32 (32 sample blocks)");

#define DEXED_POLYPHONY 16

DexedShim::DexedShim(uint32_t sample_rate)
    : Dexed(DEXED_POLYPHONY, (uint16_t)sample_rate)
{
    init(sample_rate);
}

DexedShim::~DexedShim()
{
    panic();
}

bool DexedShim::init(uint32_t sample_rate)
{
    (void)sample_rate;
    setEngineType(MSFA);
    activate();
    setGain(0.8f);
    setPBController(2, 0);     /* 2 semitone range, continuous step */
    setMWController(99, 3, 0); /* 0..99 range, target Pitch & Amp LFO mod, linear */
    setATController(99, 3, 0); /* 0..99 range, target Pitch & Amp LFO mod, linear */
    return true;
}

void DexedShim::reset()
{
    panic();
}

void DexedShim::note_on(uint8_t note, uint8_t velocity)
{
    keydown(note, velocity);
}

void DexedShim::note_off(uint8_t note, uint8_t velocity)
{
    (void)velocity;
    keyup(note);
}

void DexedShim::all_notes_off()
{
    panic();
}

void DexedShim::set_pitch_bend(float bend)
{
    int32_t raw_bend = (int32_t)(bend * 8192.0f);
    if (raw_bend > 8191) raw_bend = 8191;
    else if (raw_bend < -8192) raw_bend = -8192;
    setPitchbend((int16_t)raw_bend);
}

void DexedShim::set_mod_wheel(float value)
{
    int32_t val = (int32_t)(value * 127.0f);
    if (val > 127) val = 127;
    else if (val < 0) val = 0;
    setModWheel((uint8_t)val);
    ControllersRefresh();
}

void DexedShim::set_aftertouch(float value)
{
    int32_t val = (int32_t)(value * 127.0f);
    if (val > 127) val = 127;
    else if (val < 0) val = 0;
    setAftertouch((uint8_t)val);
    ControllersRefresh();
}

void DexedShim::set_master_volume(float volume)
{
    (void)volume;
    setGain(1.0f);
}

bool DexedShim::load_patch(uint8_t bank, uint8_t patch)
{
    if (bank < PATCH_NUM_BANKS && patch < PATCH_NUM_PATCHES_PER_BANK) {
        uint8_t decoded_voice[156];
        decodeVoice(decoded_voice, (uint8_t*)progmem_bank[bank][patch]);
        loadVoiceParameters(decoded_voice);
        return true;
    }
    return false;
}

void DexedShim::set_custom_param(uint16_t param_id, float value)
{
    (void)param_id;
    (void)value;
}

void DexedShim::render_block(float *stereo_out, size_t frames)
{
    if (stereo_out == nullptr || frames == 0) {
        return;
    }

    if (refreshVoice) {
        for (uint8_t i = 0; i < used_notes; i++) {
            if (voices[i].live) {
                voices[i].dx7_note->update(data, voices[i].midi_note, voices[i].velocity, voices[i].porta, &controllers);
            }
        }
        lfo.reset(data + 137);
        refreshVoice = false;
    }

    size_t remaining = frames;
    size_t out_idx = 0;

    while (remaining > 0) {
        size_t chunk = (remaining > _N_) ? _N_ : remaining;
        int32_t q32_buffer[_N_];
        for (size_t i = 0; i < chunk; i++) {
            q32_buffer[i] = 0;
        }

        for (size_t i = 0; i < chunk; i += _N_) {
            AlignedBuf<int32_t, _N_> audiobuf;
            for (uint8_t j = 0; j < _N_; ++j) {
                audiobuf.get()[j] = 0;
            }

            int32_t lfovalue = lfo.getsample();
            int32_t lfodelay = lfo.getdelay();

            for (uint8_t note = 0; note < used_notes; note++) {
                if (voices[note].live) {
                    voices[note].dx7_note->compute(audiobuf.get(), lfovalue, lfodelay, &controllers);
                    for (uint8_t j = 0; j < _N_; ++j) {
#ifdef USE_FILTER
                        q32_buffer[i + j] += lowpassFilter(audiobuf.get()[j]);
#else
                        q32_buffer[i + j] += audiobuf.get()[j];
#endif
                        audiobuf.get()[j] = 0;
                    }
                }
            }
        }

        for (size_t i = 0; i < chunk; i++) {
            int32_t q16_val = q32_buffer[i] >> 16;
            int32_t sample = ((q16_val * (int32_t)gain) >> 11) * 6;
            float s = (float)sample / 32767.0f;

            stereo_out[out_idx * 2 + 0] = s;
            stereo_out[out_idx * 2 + 1] = s;
            out_idx++;
        }

        remaining -= chunk;
    }
}

const EngineMetadata* DexedShim::get_metadata() const
{
    static const EngineMetadata METADATA = {
        "Dexed (FM)",
        DEXED_POLYPHONY,
        PATCH_NUM_BANKS,
        PATCH_NUM_PATCHES_PER_BANK
    };
    return &METADATA;
}
