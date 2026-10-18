/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file synth_engine.h
 * Polyphonic Synthesis Engine Interface for SMK-37 Pro
 */

#ifndef SYNTH_ENGINE_H
#define SYNTH_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_POLYPHONY 8

void synth_init(uint32_t sample_rate);
void synth_set_engine(uint8_t engine_idx);
uint8_t synth_get_engine(void);
uint8_t synth_get_num_engines(void);
const char *synth_get_engine_name(uint8_t engine_idx);

void synth_note_on(uint8_t note, uint8_t velocity);
void synth_note_off(uint8_t note);
void synth_all_notes_off(void);

void synth_set_pitch_bend(int16_t bend); /* -8192 to +8191 */
void synth_set_pitch_bend_f(float bend); /* -1.0f to +1.0f */
void synth_set_mod_wheel(uint8_t val);   /* 0 to 127 */
void synth_set_mod_wheel_f(float val);   /* 0.0f to 1.0f */
void synth_set_aftertouch(uint8_t val);  /* 0 to 127 */
void synth_set_aftertouch_f(float val);  /* 0.0f to 1.0f */
void synth_set_volume(uint8_t volume);   /* 0 to 127 */
void synth_set_volume_f(float volume);   /* 0.0f to 1.0f */
void synth_set_bank_patch(uint8_t bank, uint8_t patch);
void synth_set_custom_param(uint16_t param_id, float value);

/**
 * Poll audio engine to fill interleaved 16-bit signed stereo samples.
 */
void synth_get_samples(int16_t *buf, size_t frames);

/**
 * Get current peak audio level for UI VU meter (0..100).
 */
uint8_t synth_get_vu_level(void);

/**
 * Benchmark hook: report the true per-block cost of the MCUno-60 engine.
 *
 * The voice-path benchmark in bench.c times voice_process_sample() directly,
 * which deliberately excludes the chorus, master HPF, output conversion and
 * per-block overhead. At 6 voices that path already consumes ~95% of a block
 * period, so the fixed non-voice cost is what decides whether 6 voices is
 * reachable at all. These two calls let the benchmark drive the real
 * juno_engine_process_block() entry point that the app actually uses and
 * subtract out the remainder.
 *
 * Both no-ops unless the MCUno-60 engine is selected, so they are safe to call
 * in any state.
 */
void synth_bench_juno_set_voices(int voices);

/**
 * CPU worst-case patch: all four DCO outputs (saw + pulse + sub + noise),
 * LFO-swept PWM, max resonance with envelope and LFO modulation plus full key
 * tracking, HPF at position 3, and both chorus units.
 *
 * The factory bank0/patch0 default is a plain single-oscillator patch that
 * leaves sub, noise, PWM and the chorus idle, so it badly understates the real
 * load. Returns a pointer to a shared static instance so both the standalone
 * voice timing loop and the live engine use one identical definition.
 *
 * Any patch count measured under this one is a lower bound, i.e. genuinely safe.
 */
/* Returns the heavy patch as an opaque pointer: JunoEngineParams is a typedef of
 * an anonymous struct, so it cannot be spelled here without dragging voice.h
 * into every translation unit that includes this header (which clashes with the
 * SDK's own bool). Callers that already include voice.h cast it themselves. */
const void *synth_bench_juno_heavy_params(void);

/**
 * Install the CPU worst-case patch into the live engine. Must be called before
 * the benchmark sweeps so every voice measured exercises all per-sample paths.
 */
void synth_bench_juno_set_heavy_patch(void);

/**
 * Named factory patches for A/B listening and numerical comparison.
 *
 * The synthetic worst-case patch above is built for CPU measurement, not for
 * judging whether an optimisation is audible: it is deliberately extreme, and
 * "sounds different" there is a weak signal. These two were chosen by scanning
 * all 256 factory patches (see docs/OPTIMISATIONS.md) for patches that keep a
 * full oscillator stack alive while holding the filter in a useful regime.
 *
 * SYNTH_BENCH_JUNO_PATCH_NUMERICAL -- "A15 Moving Strings", bank 4 patch 4.
 *   saw + pulse + sub, chorus mode 2, sustain 0.69 so notes ring, attack 0.102
 *   so a note is audible quickly. Three things are modulating: PWM depth 0.307,
 *   VCF env mod 0.031, key tracking 0.874. vcfRes 0.157 maps to k=0.32 in the ladder, far below the k>3
 *   self-oscillation knee, so the filter stays linear and roundoff differences
 *   show up proportionally instead of being amplified by feedback. This is the
 *   patch to use for numeric divergence measurements.
 *
 * SYNTH_BENCH_JUNO_PATCH_LISTENING -- "A57 Thud Wah", bank 5 patch 6.
 *   The closest existing thing to the oscillator-stack brief. All three
 *   oscillators, three mod sources including the noise generator, chorus mode 2,
 *   sustain 0.80, and attack 0.000 for instant response. vcfRes 0.701 maps to
 *   k=2.79: hard ringing, just under the self-oscillation knee.
 *
 * SYNTH_BENCH_JUNO_PATCH_SELFOSC -- "B76 Martian Chord", bank 3 patch 21.
 *   True self-oscillation, and the only one of the 256 factory patches usable
 *   for it. vcfRes 0.992 gives k=4.83, well past the k>3 knee, so the filter
 *   rings on its own with no oscillator to sustain it. Attack 0.008 and sustain
 *   0.992 mean a note rings indefinitely, which is exactly what an A/B toggle
 *   needs: the filter state persists across presses instead of resetting, so a
 *   difference in the VCF arithmetic is audible against a stable tone.
 *
 *   Every oscillator is off, and that is the point rather than a compromise --
 *   the only signal in this patch is the filter's own feedback, so it isolates
 *   the VCF path from every other optimisation under test. Nothing here is
 *   driven by an envelope or LFO on the DCO side, so it will not exercise
 *   fast_exp, the noise generator, or the sub taper. Use it for the VCF, and
 *   the other two presets for everything else.
 *
 *   Chosen over the other four candidates at res >= 0.99 with attack <= 0.05 and
 *   sustain >= 0.9 ("A75 UFO", "B27 FX Rise 1", "B36 FX Sweep 1", "B72 Space
 *   Chime"). "A75 UFO" has the most modulators but its vcfCutoff is 0.000, which
 *   puts the base frequency at 28 Hz and the low end of its sweep at the 15 Hz
 *   clamp -- self-oscillation there is subsonic and inaudible on a small
 *   speaker. "B72 Space Chime" has sustain 0.28 and only key tracking driving
 *   the cutoff. This patch sweeps 97 Hz to 3.9 kHz with the LFO on top, so the
 *   ringing stays inside the audible band throughout.
 *
 * Note that resEffective is clamped to 0.999 in voice.c, and the k curve
 * compresses hard above 3.0 (res 0.992 gives k=4.83 against res 1.0 giving
 * k=4.87), so among maximum-resonance patches the differences are small.
 */
typedef enum {
    SYNTH_BENCH_JUNO_PATCH_NUMERICAL = 0, /**< A15 Moving Strings, res 0.157 */
    SYNTH_BENCH_JUNO_PATCH_LISTENING = 1, /**< A57 Thud Wah, res 0.701 */
    SYNTH_BENCH_JUNO_PATCH_SELFOSC = 2,   /**< B76 Martian Chord, res 0.992 */
    SYNTH_BENCH_JUNO_PATCH_COUNT
} SynthBenchJunoPatch;

/** Bank index of each named patch, for display in the benchmark report. */
unsigned int synth_bench_juno_patch_bank(SynthBenchJunoPatch patch);
unsigned int synth_bench_juno_patch_index(SynthBenchJunoPatch patch);

/** Short display name for a named patch, e.g. "MovingStrings". */
const char *synth_bench_juno_patch_name(SynthBenchJunoPatch patch);

/**
 * Install a named factory patch into the live engine.
 *
 * Unlike the synthetic heavy patch, this resolves the real factory data, so the
 * benchmark measures exactly what a user hears rather than an idealised
 * superset. Returns 0 if the engine does not exist or the id is out of range.
 */
int synth_bench_juno_set_patch(SynthBenchJunoPatch patch);

/**
 * Load the named patch into a caller-owned struct, without touching the live
 * engine. Lets the standalone voice timing loop in bench.c drive the same patch
 * the full-engine sweep uses. Returns 0 on a bad id.
 */
int synth_bench_juno_get_patch(SynthBenchJunoPatch patch, void *out_params);

int  synth_bench_juno_render(float *out_l, float *out_r, int32_t chunk);
unsigned int synth_bench_juno_status(unsigned int *out_init_done,
                                     unsigned int *out_juno,
                                     unsigned int *out_dexed,
                                     unsigned int *out_active);

#ifdef __cplusplus
}
#endif

#endif /* SYNTH_ENGINE_H */
