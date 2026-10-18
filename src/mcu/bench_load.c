/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file bench_load.c
 * Sustained-load CPU benchmark mode for the audio core.
 *
 * Design notes
 * ------------
 * Percentiles come from a fixed 101-bucket histogram indexed by whole-percent
 * CPU load rather than from storing samples. At 375 blocks/s a 32-bit counter
 * does not wrap for ~317 years, and 404 bytes of histogram costs nothing next
 * to the divide that sorting even a 256-sample ring would add to the audio
 * path. One-percent resolution is the right trade: the quantity being tracked
 * is a smooth load percentage, not a timing edge, and the host script prints
 * the histogram resolution alongside the values so the numbers are not
 * mistaken for exact timings.
 *
 * The per-block cost of this file is one compare, one add and one array
 * increment on the common path. Reporting runs twice a second, after the block
 * has been accounted, and the CDC ring drops bytes rather than blocking if the
 * host is not draining it.
 */

/* mcuno60_patches.h must come first. It reaches voice.h -> polyblep.h, which
 * typedefs bool for freestanding builds, and that typedef collides if a
 * newlib header has already defined bool. mcuno60_patches.c relies on the same
 * ordering. Fixing it properly means guarding the typedef in polyblep.h, which
 * is validated DSP code and out of scope here. */
#include "mcuno60_patches.h"

#include "bench_load.h"
#include "synth_engine.h"
#include "patch_banks.h"

#include <stdio.h>
#include <string.h>

/* 128 frames @ 48 kHz = 2.6667 ms. At the 24 MHz TIMER5 reference that is
 * (128 / 48000) * 24,000,000 = 64,000 ticks. Must match AUDIO_BLOCK_TICKS_24M
 * in mcu_main.c, which is what the on-screen VU meter is computed against --
 * otherwise the benchmark and the UI would disagree about the same workload. */
#define BENCH_BLOCK_TICKS   64000u

/* Percent carried to two decimal places: 100.00% == 10000. */
#define PCT_SCALE           100u

/* 750 blocks @ 375 blocks/s = 2.0 s between progress lines. Frequent enough to
 * see the distribution settle, sparse enough to stay inside the CDC budget. */
#define BENCH_REPORT_BLOCKS 750u

/* Blocks discarded before any statistic is recorded.
 *
 * Core 0 is still bringing up USB, LVGL and the app while the audio core is
 * already rendering, and that contention shows up: the deadline is missed
 * early on and never again. Measured on hardware, the first miss lands at
 * one deadline miss at about 2.1s in, reproducible at the same block on every
 * run, and none in the following 6,700. Counting it would pin max to 100% and
 * print a false "deadline reached" warning on every run, which is the one
 * signal that should be reserved for a genuine overload.
 *
 * 1,000 blocks (2.7s) are discarded, which clears that transient with margin.
 * Raising this is the lever if a real overload ever needs to be seen. The blocks are still rendered and still pushed to the DAC -- only
 * the accounting is skipped -- and the skipped count and its overruns are both
 * reported, so nothing is hidden. */
#define BENCH_WARMUP_BLOCKS 1000u

/* The chord held for the whole run. Spans C3..G4 so key tracking, the
 * sub-octave oscillator and per-voice denormals all see a realistic spread of
 * note frequencies rather than one repeated pitch. */
static const uint8_t bench_chord[] = { 48, 52, 55, 60, 64, 67 };
#define BENCH_CHORD_NOTES ((uint8_t)(sizeof(bench_chord) / sizeof(bench_chord[0])))

/* Cumulative (whole-run) statistics. */
static uint32_t bench_blocks = 0;
static uint64_t bench_sum_ticks = 0;
static uint16_t bench_max_ticks = 0;
static uint32_t bench_over = 0;
static uint32_t bench_hist[101] __attribute__((aligned(32)));

/* Published to Core 0 for formatting. Written only by the audio core, read only
 * by Core 0; seq is the hand-off flag and is incremented last. */
static volatile BenchLoadReport bench_report;

/* Warmup accounting, so the skipped blocks are visible rather than hidden. */
static uint32_t bench_warmup_left = BENCH_WARMUP_BLOCKS;
static uint32_t bench_warmup_blocks = 0;
static uint32_t bench_warmup_over = 0;

/* Block index of the first deadline miss, or 0 if there has not been one.
 * Distinguishes "missed once during startup" from "still missing", which is the
 * difference between noise and a real overload. */
static uint32_t bench_first_over_block = 0;

/* Current reporting window. */
static uint32_t bench_win_blocks = 0;
static uint64_t bench_win_sum_ticks = 0;
static uint16_t bench_win_max_ticks = 0;
static uint32_t bench_win_over = 0;
static uint32_t bench_win_hist[101] __attribute__((aligned(32)));

/* Whole-percent bucket index for a block. A block at or past the deadline is
 * clamped into the top bucket: an overrun is what the caller needs to see, and
 * how far past it went is not recoverable from this counter anyway. */
static void bench_load_publish(void);

static uint32_t pct_bucket(uint16_t ticks)
{
    if (ticks >= BENCH_BLOCK_TICKS) {
        return 100u;
    }
    return (uint32_t)ticks / (BENCH_BLOCK_TICKS / 100u);
}

/* Convert a tick count to hundredths of a percent, clamped at 100.00%. */
static uint32_t ticks_pct100(uint16_t ticks)
{
    if (ticks >= BENCH_BLOCK_TICKS) {
        return 100u * PCT_SCALE;
    }
    return ((uint32_t)ticks * PCT_SCALE) / (BENCH_BLOCK_TICKS / 100u);
}

/* Average over a window, in hundredths of a percent. */
static uint32_t avg_pct100(uint64_t sum_ticks, uint32_t n)
{
    if (n == 0u) {
        return 0u;
    }
    /* 64-bit throughout: sum_ticks * 10000 overflows 32 bits after ~1.2 hours
     * of benchmarking, which is well within a long unattended soak. */
    return (uint32_t)((sum_ticks * 10000u) / ((uint64_t)n * BENCH_BLOCK_TICKS));
}

/* Smallest whole-percent load at or below which pct of blocks fall. */
static uint32_t histogram_percentile(const uint32_t *hist, uint32_t total, uint32_t pct)
{
    uint32_t want, cum = 0, i;

    if (total == 0u) {
        return 0u;
    }
    /* Round up so p99 of 100 blocks selects the 99th sample, not the 98th. */
    want = (total * pct + 99u) / 100u;

    for (i = 0; i <= 100u; i++) {
        cum += hist[i];
        if (cum >= want) {
            return i * PCT_SCALE;
        }
    }
    return 100u * PCT_SCALE;
}

uint8_t bench_load_engine(void)
{
    return SYNTH_ENGINE_MCUNO60;
}

uint8_t bench_load_bank(void)
{
    return MCUNO60_BENCH_BANK;
}

uint8_t bench_load_patch(void)
{
    /* Last entry in the benchmark bank: the synthetic CPU worst case. The
     * factory-derived presets occupy the lower indices. */
    return MCUNO60_BENCH_PATCHES - 1;
}

void bench_load_init(void)
{
    memset(bench_hist, 0, sizeof(bench_hist));
    memset(bench_win_hist, 0, sizeof(bench_win_hist));
    bench_blocks = 0;
    bench_sum_ticks = 0;
    bench_max_ticks = 0;
    bench_over = 0;
    bench_win_blocks = 0;
    bench_win_sum_ticks = 0;
    bench_win_max_ticks = 0;
    bench_win_over = 0;

    /* Select MCUno-60 and install the CPU worst case. Both must happen after
     * synth_init(), hence the call site in cpu1_entry(). */
    synth_set_engine(bench_load_engine());
    synth_bench_juno_set_heavy_patch();

    /* Release anything the boot sequence left ringing so the measured voice
     * count is exactly the chord below and not boot-dependent. */
    synth_all_notes_off();

    for (uint8_t i = 0; i < BENCH_CHORD_NOTES; i++) {
        synth_note_on(bench_chord[i], 127);
    }

    printf("BENCHLOAD ready voices=%u patch=heavy bank=0/3 chord=%u,%u,%u,%u,%u,%u\n",
           BENCH_CHORD_NOTES,
           bench_chord[0], bench_chord[1], bench_chord[2],
           bench_chord[3], bench_chord[4], bench_chord[5]);
}

void bench_load_on_block(uint32_t ticks, int16_t *pcm, size_t frames)
{
    /* Silence before the DAC, after the render has been measured. The DMA, the
     * buffer ping-pong and the audio interrupt all keep running so the timing
     * characteristics stay those of normal playback; only the samples are
     * forced to zero so an unattended unit cannot make noise. This runs on warmup
     * blocks too, so the audio path is identical from the very first block. */
    if (pcm != NULL && frames > 0) {
        memset(pcm, 0, frames * 2 * sizeof(int16_t));
    }

    if (bench_warmup_left > 0u) {
        bench_warmup_left--;
        bench_warmup_blocks++;
        if (ticks >= BENCH_BLOCK_TICKS) {
            bench_warmup_over++;
        }
        return;
    }

    uint32_t bucket = pct_bucket(ticks);

    bench_blocks++;
    bench_sum_ticks += ticks;
    if (ticks > bench_max_ticks) {
        bench_max_ticks = ticks;
    }
    if (ticks >= BENCH_BLOCK_TICKS) {
        if (bench_first_over_block == 0u) {
            bench_first_over_block = bench_blocks; /* bench_blocks already incremented */
        }
        bench_over++;
    }
    bench_hist[bucket]++;

    bench_win_blocks++;
    bench_win_sum_ticks += ticks;
    if (ticks > bench_win_max_ticks) {
        bench_win_max_ticks = ticks;
    }
    if (ticks >= BENCH_BLOCK_TICKS) {
        bench_win_over++;
    }
    bench_win_hist[bucket]++;

    if (bench_win_blocks >= BENCH_REPORT_BLOCKS) {
        bench_load_publish();
        bench_win_blocks = 0;
        bench_win_sum_ticks = 0;
        bench_win_max_ticks = 0;
        bench_win_over = 0;
        memset(bench_win_hist, 0, sizeof(bench_win_hist));
    }
}

/* Recompute the summary into the published block. Audio-core context: pure
 * arithmetic over data already in cache, no formatting and no I/O. */
static void bench_load_publish(void)
{
    /* Read seq first and discard a torn snapshot rather than publishing one.
     * Only one writer exists, so the second read cannot see a newer value than
     * the first. */
    uint32_t seq = bench_report.seq;

    bench_report.blocks = bench_blocks;
    bench_report.avg_pct100 = avg_pct100(bench_sum_ticks, bench_blocks);
    bench_report.p99_pct100 = histogram_percentile(bench_hist, bench_blocks, 99u);
    bench_report.max_pct100 = ticks_pct100(bench_max_ticks);
    bench_report.over = bench_over;
    bench_report.first_over_block = bench_first_over_block;
    bench_report.warmup_blocks = bench_warmup_blocks;
    bench_report.warmup_over = bench_warmup_over;
    bench_report.win_blocks = bench_win_blocks;
    bench_report.win_avg_pct100 = avg_pct100(bench_win_sum_ticks, bench_win_blocks);
    bench_report.win_max_pct100 = ticks_pct100(bench_win_max_ticks);
    bench_report.win_over = bench_win_over;

    /* Release: everything above must be visible before the new seq. */
    __asm__ volatile("" ::: "memory");
    bench_report.seq = seq + 1;
}

/* Last seq already handed to the printer. Static so that repeated calls with no
 * intervening publish report nothing new; without this the 10 ms caller would
 * re-print the same report a hundred times a second. */
static uint32_t bench_printed_seq = 0;

int bench_load_take(BenchLoadReport *out)
{
    uint32_t before, after;

    if (out == NULL) {
        return 0;
    }
    before = bench_report.seq;
    if (before == 0u || before == bench_printed_seq) {
        return 0; /* nothing published, or nothing newer */
    }
    bench_printed_seq = before;
    __asm__ volatile("" ::: "memory");
    *out = bench_report;
    __asm__ volatile("" ::: "memory");
    after = bench_report.seq;

    /* A change across the copy means the audio core republished mid-read, so
     * this snapshot mixes two reports. Drop it; the next poll gets a clean one
     * two seconds later, which is a far better outcome than a garbled figure.
     * Give the sequence number back so the newer report is still delivered. */
    if (before != after) {
        bench_printed_seq = before;
        return 0;
    }
    return 1;
}

void bench_load_report_print(void)
{
    BenchLoadReport r;

    if (!bench_load_take(&r)) {
        return;
    }

    /* Every record repeats the voice count so each line stands alone. The host
     * may attach after boot and miss the one-shot BENCHLOAD banner, and keying
     * the parser off these periodic records instead of a single early banner is
     * what makes collection robust across that race. */
    printf("BENCHCPU v=%u n=%u avg=%u.%02u p99=%u.%02u max=%u.%02u over=%u fo=%u"
           " | warm_n=%u warm_over=%u"
           " | win_n=%u win_avg=%u.%02u win_max=%u.%02u win_over=%u\n",
           BENCH_CHORD_NOTES,
           r.blocks,
           r.avg_pct100 / 100u, r.avg_pct100 % 100u,
           r.p99_pct100 / 100u, r.p99_pct100 % 100u,
           r.max_pct100 / 100u, r.max_pct100 % 100u,
           r.over,
           r.first_over_block,
           r.warmup_blocks,
           r.warmup_over,
           r.win_blocks,
           r.win_avg_pct100 / 100u, r.win_avg_pct100 % 100u,
           r.win_max_pct100 / 100u, r.win_max_pct100 % 100u,
           r.win_over);
}
