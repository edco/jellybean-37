/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file juno_shim.h
 * Roland Juno-60 Engine Shim wrapping JunoEngine and subclassing SynthEngine
 */

#ifndef JUNO_SHIM_H
#define JUNO_SHIM_H

#include "synth_engine_interface.h"
#include "juno_engine.h"
#include "synth_engine.h"

class JunoShim final : public SynthEngine {
public:
    explicit JunoShim(uint32_t sample_rate);
    ~JunoShim() override = default;

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

    void render_block(int16_t *stereo_out, size_t frames) override;
    const EngineMetadata* get_metadata() const override;

    /* Benchmark and diagnostic helpers */
    void bench_set_voices(int voices);
    void bench_set_heavy_patch();
    int bench_set_patch(SynthBenchJunoPatch patch);
    int bench_render(float *out_l, float *out_r, int32_t chunk);
    unsigned int get_active_voice_count() const;
    JunoEngine* get_raw_engine() { return &engine; }

private:
    JunoEngine engine;
    float master_volume;
    float mod_wheel_val;
    float bender_val;
};

#endif /* JUNO_SHIM_H */
