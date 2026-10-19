/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file dexed_shim.h
 * Dexed FM Synthesis Engine Shim subclassing SynthEngine
 */

#ifndef DEXED_SHIM_H
#define DEXED_SHIM_H

#include "synth_engine_interface.h"
#include "dexed.h"

class DexedShim final : public SynthEngine, public Dexed {
public:
    explicit DexedShim(uint32_t sample_rate);
    ~DexedShim() override;

    bool init(uint32_t sample_rate) override;
    void reset() override;

    void note_on(uint8_t note, uint8_t velocity) override;
    void note_off(uint8_t note, uint8_t velocity = 0) override;
    void all_notes_off() override;

    void set_pitch_bend(float bend) override;
    void set_mod_wheel(float value) override;
    void set_aftertouch(float value) override;
    void set_master_volume(float volume) override;

    bool load_patch(uint8_t bank, uint8_t patch) override;
    void set_custom_param(uint16_t param_id, float value) override;

    void render_block(float *stereo_out, size_t frames) override;
    const EngineMetadata* get_metadata() const override;
};

#endif /* DEXED_SHIM_H */
