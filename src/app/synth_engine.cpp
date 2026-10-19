/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file synth_engine.cpp
 * Polyphonic Synthesis Engine Dispatcher (Dexed & MCUno-60)
 */

#include "synth_engine.h"
#include "synth_engine_interface.h"
#include "dexed_shim.h"
#include "juno_shim.h"
#include "patch_banks.h"
#include "soft_limiter.h"
#include <new>
#include <string.h>

constexpr size_t MAX_ENGINE_SIZE = sizeof(DexedShim) > sizeof(JunoShim) ? sizeof(DexedShim) : sizeof(JunoShim);
alignas(32) static uint8_t engine_arena[MAX_ENGINE_SIZE];

static SoftLimiterPade s_limiter;

static SynthEngine *current_engine = nullptr;
static uint8_t current_engine_idx = SYNTH_ENGINE_DEXED;
static uint32_t engine_sample_rate = 48000;
static float master_volume = 1.0f;
static float mod_wheel_val = 0.0f;
static float bender_val = 0.0f;
static float aftertouch_val = 0.0f;
static uint8_t current_bank = 0;
static uint8_t current_patch = 0;
static uint8_t peak_vu = 0;
static unsigned int init_done = 0;

void synth_init(uint32_t sample_rate)
{
    engine_sample_rate = (sample_rate > 0) ? sample_rate : 48000;

    if (current_engine != nullptr) {
        current_engine->all_notes_off();
        current_engine->~SynthEngine();
        current_engine = nullptr;
    }

    current_engine_idx = SYNTH_ENGINE_DEXED;
    current_engine = new (engine_arena) DexedShim(engine_sample_rate);

    current_engine->set_master_volume(master_volume);
    current_engine->set_pitch_bend(bender_val);
    current_engine->set_mod_wheel(mod_wheel_val);
    current_engine->set_aftertouch(aftertouch_val);
    current_bank = 0;
    current_patch = 0;
    current_engine->load_patch(current_bank, current_patch);

    peak_vu = 0;
    soft_limiter_init(&s_limiter, SOFT_LIMITER_DEFAULT_THRESHOLD, SOFT_LIMITER_DEFAULT_MAX);
    init_done = 1;
}

void synth_set_engine(uint8_t engine_idx)
{
    if (engine_idx >= SYNTH_NUM_ENGINES) {
        return;
    }
    if (current_engine != nullptr && current_engine_idx == engine_idx) {
        return;
    }

    if (current_engine != nullptr) {
        current_engine->all_notes_off();
        current_engine->~SynthEngine();
        current_engine = nullptr;
    }

    current_engine_idx = engine_idx;
    if (engine_idx == SYNTH_ENGINE_DEXED) {
        current_engine = new (engine_arena) DexedShim(engine_sample_rate);
    } else if (engine_idx == SYNTH_ENGINE_MCUNO60) {
        current_engine = new (engine_arena) JunoShim(engine_sample_rate);
    }

    if (current_engine != nullptr) {
        current_engine->set_master_volume(master_volume);
        current_engine->set_pitch_bend(bender_val);
        current_engine->set_mod_wheel(mod_wheel_val);
        current_engine->set_aftertouch(aftertouch_val);
        current_bank = 0;
        current_patch = 0;
        current_engine->load_patch(0, 0);
    }
}

uint8_t synth_get_engine(void)
{
    return current_engine_idx;
}

uint8_t synth_get_num_engines(void)
{
    return SYNTH_NUM_ENGINES;
}

const char *synth_get_engine_name(uint8_t engine_idx)
{
    return patch_banks_get_engine_name(engine_idx);
}

void synth_set_bank_patch(uint8_t bank, uint8_t patch)
{
    current_bank = bank;
    current_patch = patch;
    if (current_engine != nullptr) {
        current_engine->load_patch(bank, patch);
    }
}

void synth_set_custom_param(uint16_t param_id, float value)
{
    if (current_engine != nullptr) {
        current_engine->set_custom_param(param_id, value);
    }
}

void synth_note_on(uint8_t note, uint8_t velocity)
{
    if (current_engine != nullptr) {
        current_engine->note_on(note, velocity);
    }
}

void synth_note_off(uint8_t note)
{
    if (current_engine != nullptr) {
        current_engine->note_off(note);
    }
}

void synth_all_notes_off(void)
{
    if (current_engine != nullptr) {
        current_engine->all_notes_off();
    }
}

void synth_set_pitch_bend_f(float bend)
{
    bender_val = bend;
    if (current_engine != nullptr) {
        current_engine->set_pitch_bend(bender_val);
    }
}

void synth_set_pitch_bend(int16_t bend)
{
    synth_set_pitch_bend_f((float)bend / 8192.0f);
}

void synth_set_mod_wheel_f(float val)
{
    mod_wheel_val = val;
    if (current_engine != nullptr) {
        current_engine->set_mod_wheel(mod_wheel_val);
    }
}

void synth_set_mod_wheel(uint8_t val)
{
    synth_set_mod_wheel_f((float)val / 127.0f);
}

void synth_set_aftertouch_f(float val)
{
    aftertouch_val = val;
    if (current_engine != nullptr) {
        current_engine->set_aftertouch(aftertouch_val);
    }
}

void synth_set_aftertouch(uint8_t val)
{
    synth_set_aftertouch_f((float)val / 127.0f);
}

void synth_set_volume_f(float volume)
{
    if (volume < 0.0f) volume = 0.0f;
    master_volume = volume;
    if (current_engine != nullptr) {
        current_engine->set_master_volume(master_volume);
    }
}

void synth_set_volume(uint8_t volume)
{
    synth_set_volume_f((float)volume / 100.0f);
}

uint8_t synth_get_vu_level(void)
{
    return peak_vu;
}

void synth_set_limiter(float threshold, float max_ceiling)
{
    soft_limiter_init(&s_limiter, threshold, max_ceiling);
}

void synth_get_limiter(float *threshold, float *max_ceiling)
{
    if (threshold != nullptr) *threshold = s_limiter.T;
    if (max_ceiling != nullptr) *max_ceiling = s_limiter.M;
}

void synth_get_samples(int16_t *buf, size_t frames)
{
    if (buf == nullptr || frames == 0) {
        return;
    }

    if (current_engine == nullptr) {
        memset(buf, 0, frames * 2 * sizeof(int16_t));
        return;
    }

    float fbuf[64]; /* 32 stereo frames */
    size_t remaining = frames;
    size_t out_idx = 0;
    int32_t peak = 0;

    /* Natural audio-taper perceptual curve applied strictly BEFORE the soft limiter:
     * gain = v * (v + 0.5) / 1.5 where v = master_volume (1.0 at vol 100).
     * At vol 100 (v=1.0): gain = 1.00 (0 dB unity).
     * At vol 127 (v=1.27): gain = 1.50 (+3.5 dB overdrive boost).
     * At vol 50 (v=0.5): gain = 0.333 (-9.5 dB).
     * At vol 0 (v=0.0): gain = 0.0. */
    float v = master_volume;
    float vol_gain = (v <= 0.0f) ? 0.0f : (v * (v + 0.5f) * (1.0f / 1.5f));

    while (remaining > 0) {
        size_t chunk = (remaining > 32) ? 32 : remaining;
        current_engine->render_block(fbuf, chunk);

        for (size_t i = 0; i < chunk; i++) {
            float in_l = fbuf[i * 2 + 0] * vol_gain;
            float in_r = fbuf[i * 2 + 1] * vol_gain;

            float l = soft_limit_branchy(&s_limiter, in_l);
            float r = soft_limit_branchy(&s_limiter, in_r);

            int32_t samp_l = (int32_t)(l * 32767.0f);
            int32_t samp_r = (int32_t)(r * 32767.0f);

            if (samp_l > 32767) samp_l = 32767;
            else if (samp_l < -32768) samp_l = -32768;

            if (samp_r > 32767) samp_r = 32767;
            else if (samp_r < -32768) samp_r = -32768;

            buf[out_idx * 2 + 0] = (int16_t)samp_l;
            buf[out_idx * 2 + 1] = (int16_t)samp_r;

            int32_t abs_l = (samp_l < 0) ? -samp_l : samp_l;
            int32_t abs_r = (samp_r < 0) ? -samp_r : samp_r;
            if (abs_l > peak) peak = abs_l;
            if (abs_r > peak) peak = abs_r;

            out_idx++;
        }

        remaining -= chunk;
    }

    uint8_t current_vu = (uint8_t)((peak * 100) / 32767);
    if (current_vu > 100) current_vu = 100;

    if (current_vu > peak_vu) {
        peak_vu = current_vu;
    } else if (peak_vu > 0) {
        peak_vu--;
    }
}

extern "C" {

void synth_bench_juno_set_voices(int voices)
{
    if (current_engine_idx != SYNTH_ENGINE_MCUNO60) {
        synth_set_engine(SYNTH_ENGINE_MCUNO60);
    }
    if (current_engine != nullptr) {
        static_cast<JunoShim*>(current_engine)->bench_set_voices(voices);
    }
}

void synth_bench_juno_set_heavy_patch(void)
{
    if (current_engine_idx != SYNTH_ENGINE_MCUNO60) {
        synth_set_engine(SYNTH_ENGINE_MCUNO60);
    }
    if (current_engine != nullptr) {
        static_cast<JunoShim*>(current_engine)->bench_set_heavy_patch();
    }
}

int synth_bench_juno_set_patch(SynthBenchJunoPatch patch)
{
    if (current_engine_idx != SYNTH_ENGINE_MCUNO60) {
        synth_set_engine(SYNTH_ENGINE_MCUNO60);
    }
    if (current_engine != nullptr) {
        return static_cast<JunoShim*>(current_engine)->bench_set_patch(patch);
    }
    return 0;
}

int synth_bench_juno_render(float *out_l, float *out_r, int32_t chunk)
{
    if (current_engine_idx == SYNTH_ENGINE_MCUNO60 && current_engine != nullptr) {
        return static_cast<JunoShim*>(current_engine)->bench_render(out_l, out_r, chunk);
    }
    return 0;
}

unsigned int synth_bench_juno_status(unsigned int *out_init_done,
                                     unsigned int *out_juno,
                                     unsigned int *out_dexed,
                                     unsigned int *out_active)
{
    *out_init_done = init_done;
    *out_juno = (current_engine_idx == SYNTH_ENGINE_MCUNO60 && current_engine != nullptr)
                    ? (unsigned int)(unsigned long)static_cast<JunoShim*>(current_engine)->get_raw_engine()
                    : 0u;
    *out_dexed = (current_engine_idx == SYNTH_ENGINE_DEXED && current_engine != nullptr)
                    ? (unsigned int)(unsigned long)current_engine
                    : 0u;
    *out_active = 0u;

    if (current_engine_idx == SYNTH_ENGINE_MCUNO60 && current_engine != nullptr) {
        *out_active = static_cast<JunoShim*>(current_engine)->get_active_voice_count();
    }
    return init_done;
}

} /* extern "C" */
