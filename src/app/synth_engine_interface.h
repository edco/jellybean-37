/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file synth_engine_interface.h
 * Abstract Base Interface for Polyphonic Synthesis Engines
 */

#ifndef SYNTH_ENGINE_INTERFACE_H
#define SYNTH_ENGINE_INTERFACE_H

#include <stdint.h>
#include <stddef.h>

struct EngineMetadata {
    const char *name;
    uint8_t max_polyphony;
    uint8_t num_banks;
    uint8_t num_patches_per_bank;
};

class SynthEngine {
public:
    virtual ~SynthEngine() = default;

    virtual bool init(uint32_t sample_rate) = 0;
    virtual void reset() = 0;

    virtual void note_on(uint8_t note, uint8_t velocity) = 0;
    virtual void note_off(uint8_t note, uint8_t velocity = 0) = 0;
    virtual void all_notes_off() = 0;

    virtual void set_pitch_bend(float bend) = 0;
    virtual void set_mod_wheel(float value) = 0;
    virtual void set_aftertouch(float value) = 0;
    virtual void set_master_volume(float volume) = 0;

    virtual bool load_patch(uint8_t bank, uint8_t patch) = 0;
    virtual void set_custom_param(uint16_t param_id, float value) = 0;

    virtual void render_block(float *stereo_out, size_t frames) = 0;
    virtual const EngineMetadata* get_metadata() const = 0;
};

#endif /* SYNTH_ENGINE_INTERFACE_H */
