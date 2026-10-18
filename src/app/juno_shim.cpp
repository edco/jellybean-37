/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file juno_shim.cpp
 * Roland Juno-60 Engine Shim Implementation
 */

#include "juno_shim.h"
#include "mcuno60_patches.h"
#include <string.h>

JunoShim::JunoShim(uint32_t sample_rate)
    : master_volume(0.8f)
    , mod_wheel_val(0.0f)
    , bender_val(0.0f)
{
    memset(&engine, 0, sizeof(engine));
    init(sample_rate);
}

bool JunoShim::init(uint32_t sample_rate)
{
    juno_engine_init(&engine, (float)sample_rate);
    load_patch(0, 0);
    engine.params.master_volume = master_volume;
    engine.params.mod_wheel = mod_wheel_val;
    engine.params.bender = bender_val;
    return true;
}

void JunoShim::reset()
{
    juno_engine_all_notes_off(&engine);
}

void JunoShim::note_on(uint8_t note, uint8_t velocity)
{
    float vel = (float)velocity / 127.0f;
    juno_engine_note_on(&engine, (int32_t)note, vel);
}

void JunoShim::note_off(uint8_t note, uint8_t velocity)
{
    (void)velocity;
    juno_engine_note_off(&engine, (int32_t)note);
}

void JunoShim::all_notes_off()
{
    juno_engine_all_notes_off(&engine);
}

void JunoShim::set_pitch_bend(float bend)
{
    bender_val = bend;
    juno_engine_set_param(&engine, JUNO_PARAM_BENDER, bender_val);
}

void JunoShim::set_mod_wheel(float value)
{
    mod_wheel_val = value;
    juno_engine_set_param(&engine, JUNO_PARAM_MOD_WHEEL, mod_wheel_val);
}

void JunoShim::set_aftertouch(float value)
{
    (void)value;
}

void JunoShim::set_master_volume(float volume)
{
    master_volume = volume;
    juno_engine_set_param(&engine, JUNO_PARAM_MASTER_VOL, master_volume);
}

bool JunoShim::load_patch(uint8_t bank, uint8_t patch)
{
    if (bank < MCUNO60_NUM_BANKS && patch < mcuno60_get_bank_patch_count(bank)) {
        const JunoEngineParams *p = mcuno60_get_patch_params(bank, patch);
        if (p != nullptr) {
            engine.params = *p;
            engine.params.master_volume = master_volume;
            engine.params.mod_wheel = mod_wheel_val;
            engine.params.bender = bender_val;
            hpf_compute_coefficients(&engine.master_hpf, engine.params.hpf_position);
            engine.chorus.mode = engine.params.chorus_mode;
            return true;
        }
    }
    return false;
}

void JunoShim::set_custom_param(uint16_t param_id, float value)
{
    juno_engine_set_param(&engine, (int32_t)param_id, value);
}

void JunoShim::render_block(int16_t *stereo_out, size_t frames)
{
    if (stereo_out == nullptr || frames == 0) {
        return;
    }

    size_t remaining = frames;
    size_t out_idx = 0;
    float out_l[32];
    float out_r[32];

    while (remaining > 0) {
        size_t chunk = (remaining > 32) ? 32 : remaining;
        juno_engine_process_block(&engine, out_l, out_r, (int32_t)chunk);

        for (size_t i = 0; i < chunk; i++) {
            int32_t sample_l = (int32_t)(out_l[i] * 32767.0f);
            int32_t sample_r = (int32_t)(out_r[i] * 32767.0f);

            if (sample_l > 32767) sample_l = 32767;
            else if (sample_l < -32768) sample_l = -32768;

            if (sample_r > 32767) sample_r = 32767;
            else if (sample_r < -32768) sample_r = -32768;

            stereo_out[out_idx * 2 + 0] = (int16_t)sample_l;
            stereo_out[out_idx * 2 + 1] = (int16_t)sample_r;
            out_idx++;
        }

        remaining -= chunk;
    }
}

const EngineMetadata* JunoShim::get_metadata() const
{
    static const EngineMetadata METADATA = {
        "MCUno-60 (Juno)",
        JUNO_NUM_VOICES,
        MCUNO60_NUM_BANKS,
        MCUNO60_PATCHES_PER_BANK
    };
    return &METADATA;
}

void JunoShim::bench_set_voices(int voices)
{
    juno_engine_all_notes_off(&engine);
    for (int i = 0; i < voices && i < JUNO_NUM_VOICES; i++) {
        juno_engine_note_on(&engine, (int32_t)(48 + i), 1.0f);
    }
}

void JunoShim::bench_set_heavy_patch()
{
    const JunoEngineParams *p = (const JunoEngineParams *)synth_bench_juno_heavy_params();
    if (p != nullptr) {
        engine.params = *p;
    }
}

int JunoShim::bench_set_patch(SynthBenchJunoPatch patch)
{
    JunoEngineParams p;
    if (synth_bench_juno_get_patch(patch, &p) == 0) {
        return 0;
    }
    engine.params = p;
    return 1;
}

int JunoShim::bench_render(float *out_l, float *out_r, int32_t chunk)
{
    juno_engine_process_block(&engine, out_l, out_r, chunk);
    return 1;
}

unsigned int JunoShim::get_active_voice_count() const
{
    unsigned int active = 0;
    for (int v = 0; v < JUNO_NUM_VOICES; v++) {
        if (engine.voices[v].active) {
            active++;
        }
    }
    return active;
}

/* Benchmark patch accessors (C linkage) */
extern "C" {

const void *synth_bench_juno_heavy_params(void)
{
    return (const void *)mcuno60_get_patch_params(MCUNO60_BENCH_BANK,
                                                  MCUNO60_BENCH_PATCHES - 1);
}

static const uint8_t BENCH_PATCH_BANK[SynthBenchJunoPatch::SYNTH_BENCH_JUNO_PATCH_COUNT] = {
    MCUNO60_BENCH_BANK, /* A15 Moving Strings */
    MCUNO60_BENCH_BANK, /* A57 Thud Wah        */
    MCUNO60_BENCH_BANK, /* B76 Martian Chord   */
};

static const uint8_t BENCH_PATCH_INDEX[SynthBenchJunoPatch::SYNTH_BENCH_JUNO_PATCH_COUNT] = {
    0, /* A15 Moving Strings */
    1, /* A57 Thud Wah        */
    2, /* B76 Martian Chord  */
};

static const char *const BENCH_PATCH_NAME[SynthBenchJunoPatch::SYNTH_BENCH_JUNO_PATCH_COUNT] = {
    "MovingStrings", /* A15, res 0.157, numerical  */
    "ThudWah",       /* A57, res 0.701, listening  */
    "MartianChord",  /* B76, res 0.992, self-osc   */
};

unsigned int synth_bench_juno_patch_bank(SynthBenchJunoPatch patch)
{
    if ((unsigned int)patch >= (unsigned int)SynthBenchJunoPatch::SYNTH_BENCH_JUNO_PATCH_COUNT) {
        return 0u;
    }
    return BENCH_PATCH_BANK[patch];
}

unsigned int synth_bench_juno_patch_index(SynthBenchJunoPatch patch)
{
    if ((unsigned int)patch >= (unsigned int)SynthBenchJunoPatch::SYNTH_BENCH_JUNO_PATCH_COUNT) {
        return 0u;
    }
    return BENCH_PATCH_INDEX[patch];
}

const char *synth_bench_juno_patch_name(SynthBenchJunoPatch patch)
{
    if ((unsigned int)patch >= (unsigned int)SynthBenchJunoPatch::SYNTH_BENCH_JUNO_PATCH_COUNT) {
        return "?";
    }
    return BENCH_PATCH_NAME[patch];
}

int synth_bench_juno_get_patch(SynthBenchJunoPatch patch, void *out_params)
{
    if ((out_params == NULL) ||
        ((unsigned int)patch >= (unsigned int)SynthBenchJunoPatch::SYNTH_BENCH_JUNO_PATCH_COUNT)) {
        return 0;
    }

    const JunoEngineParams *src =
        mcuno60_get_patch_params(BENCH_PATCH_BANK[patch], BENCH_PATCH_INDEX[patch]);

    if (src == NULL) {
        return 0;
    }

    memcpy((JunoEngineParams *)out_params, src, sizeof(JunoEngineParams));
    return 1;
}

} /* extern "C" */
