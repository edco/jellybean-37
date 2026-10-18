/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file bench_load.h
 * Sustained-load CPU benchmark mode for the audio core.
 *
 * Complements the boot-time microbenchmarks in bench.c: those measure isolated
 * operations and small voice counts, whereas this holds a steady worst-case
 * polyphony and reports the distribution of *whole-block* CPU load, which is
 * the number that actually predicts buffer underruns.
 *
 * Results are emitted over the USB CDC as single-line, fixed-key records so a
 * host script can parse them without a terminal-state parser:
 *
 *   BENCHCPU n=... avg=.. p99=.. max=.. over=.. cum_n=.. cum_avg=.. ...
 *
 * Enable with -DMCUNO_BENCH_MODE=ON. The build is otherwise identical to a
 * normal image so the measurement is representative; only boot-time diagnostic
 * logging is dropped (see mcu_main.c).
 */

#ifndef BENCH_LOAD_H
#define BENCH_LOAD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Engine / bank / patch the benchmark pins the synthesizer to.
 *
 * Exposed as functions rather than macros so mcu_main.c can pin its change
 * detection without including mcuno60_patches.h: that header pulls in voice.h,
 * which typedefs bool, and mcu_main.c has already pulled the SDK's own bool
 * definition in by then. The SDK headers must not be modified to fix this.
 */
uint8_t bench_load_engine(void);
uint8_t bench_load_bank(void);
uint8_t bench_load_patch(void);

/**
 * Install the worst-case patch, hold a fixed 6-note chord and reset statistics.
 *
 * Call once from the audio core after synth_init() and before the first audio
 * block, so no partial-block sample pollutes the first data point.
 */
void bench_load_init(void);

/**
 * Account one rendered block and silence it before it reaches the DAC.
 *
 * @param ticks   Elapsed TIMER5 ticks for the render call. The deadline for a
 *                128-frame block at 48 kHz / 24 MHz is 64,000 ticks.
 * @param pcm     Rendered interleaved stereo samples; zeroed in place.
 * @param frames  Number of frames (not samples) in pcm.
 *
 * The render cost is measured by the caller immediately after the render call
 * and passed in, so this function's own statistics bookkeeping and the
 * zero-fill are both excluded from the reported figure.
 */
void bench_load_on_block(uint32_t ticks, int16_t *pcm, size_t frames);

/**
 * A snapshot of the load statistics, published by the audio core.
 *
 * The audio core does no formatting and no serial I/O of its own: at six
 * worst-case voices the render loop is already close to its deadline, and a
 * character-at-a-time CDC printf inside it is enough to blow that deadline.
 * It computes these numbers and publishes them; Core 0 formats and transmits.
 */
typedef struct {
    uint32_t seq;           /* Incremented last; a change means the rest is valid. */
    uint32_t blocks;        /* Cumulative blocks accounted. */
    uint32_t avg_pct100;    /* Cumulative mean load, hundredths of a percent. */
    uint32_t p99_pct100;    /* Cumulative 99th percentile, hundredths of a percent. */
    uint32_t max_pct100;    /* Worst single block, hundredths of a percent. */
    uint32_t over;          /* Blocks that reached the deadline. */
    uint32_t first_over_block; /* Block index of the first miss, 0 if none. */
    uint32_t warmup_blocks; /* Blocks rendered but excluded from all stats. */
    uint32_t warmup_over;   /* How many of those missed the deadline. */
    uint32_t win_blocks;    /* Trailing window, same fields. */
    uint32_t win_avg_pct100;
    uint32_t win_max_pct100;
    uint32_t win_over;
} BenchLoadReport;

/**
 * Copy the most recent report published by the audio core.
 *
 * Call from Core 0 (or any non-real-time context) on a slower cadence.
 * @return 1 if a new report was available and *out was filled, else 0.
 */
int bench_load_take(BenchLoadReport *out);

/**
 * Format and print the current statistics. Non-real-time context only.
 */
void bench_load_report_print(void);

#ifdef __cplusplus
}
#endif

#endif /* BENCH_LOAD_H */