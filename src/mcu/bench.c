/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file bench.c
 * @brief On-target microbenchmarks for pi32v2 scalar FP behaviour.
 *
 * See bench.h for rationale. Everything here runs once at boot, after the
 * core clock is configured and before Core 1 / the audio task starts,
 * and reports over the debug UART.
 *
 * ---------------------------------------------------------------------------
 * Timebase
 * ---------------------------------------------------------------------------
 * The obvious choice, local_timer_us_time(), does NOT work here. It reads a
 * software counter that is only advanced by the timer1 interrupt service
 * routine, and timer1's hardware is never started this early:
 *
 *     timer1_init:   request_irq(...)         <- no hardware start
 *     timer1_isr:    [r0+0] |= 0x4000;         <- re-arms, and bumps the counter
 *     sys_timer_init: os_sem_create(); timer1_init(); task_create();
 *
 * So before the OS tick is running, local_timer_us_time() returns a constant
 * and every measured span is exactly 0. That is not a subtle resolution
 * problem, it is a stopped timebase.
 *
 * Instead we drive JL_TIMER5 as a free-running 24 MHz counter, the same
 * peripheral and configuration Core 1 already uses for its CPU-load metric in
 * mcu_main.c. main() deliberately stops TIMER5 before doing anything else:
 *
 *     *(volatile uint32_t *)0x00010900 = 0;      // Stop TIMER5
 *
 * and holds Core 1 in reset, releasing it only later (after this benchmark),
 * so Core 0 has the core to itself while measuring. Reconfiguring TIMER5 here
 * is safe: cpu1_entry() rewrites all three registers before starting its own
 * profiling, so nothing downstream inherits our configuration.
 *
 * TIMER5 auto-reloads at PRD, and mcu_main.c sets PRD = 0xFFFF, giving a 16-bit
 * counter that wraps every 65536 / 24e6 = 2.730 ms. Single measurements are
 * therefore chunked (bench_measure) so no individual delta can wrap, and
 * chunk totals are accumulated in 64-bit arithmetic.
 *
 * Methodology notes:
 *  - Every measured region is subtracted against an identical-chunking
 *    baseline so the reported cost is the arithmetic, not loop bookkeeping.
 *  - All inner-loop arithmetic sits inside "volatile" inline asm. That stops
 *    the compiler from hoisting the loop-invariant product out of the loop
 *    (which would turn the multiply-add case into a bare add and make the
 *    comparison meaningless) and keeps it from constant-folding the chain.
 *  - Raw tick counts are printed alongside the derived nanoseconds so a dead
 *    or misconfigured timebase is obvious in the output instead of silently
 *    reporting 0.000 ns.
 */

#include "bench.h"
#include "juno_engine.h"
#include "voice.h"
#include "mcuno60_patches.h"
#include "synth_engine.h"
#include "mcu_cdc.h"
#include "asm/WL82.h"

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

static void bench_cdc_diag(const char *tag);
static inline uint32_t bench_ticks(void);

/*
 * Paced printf.
 *
 * The report truncates at a byte-stable ~1993-1997 bytes across two builds whose
 * report content differed, so the limit is a fixed output budget, not the
 * capture and not a CPU halt (later pad/ADC output does get through).
 *
 * The SDK declares its own printf/putchar/putbyte (system/generic/printf.h) and
 * routes them through a software buffer in front of JL_UART2. Emitting the whole
 * report back-to-back overruns that budget and the remainder is dropped.
 *
 * Pacing putchar() did NOT help, because putchar drives the hardware FIFO
 * directly (and correctly blocks on CON0 BIT(15) TX-not-busy). That drained the
 * wrong stage -- the software buffer printf writes into is what fills.
 *
 * So pace printf itself, using the byte count it reports, which also handles the
 * per-line cost of formatting floats and %%.
 */
#define BENCH_PRINTF_PACE_US_PER_BYTE 120u
#define BENCH_PRINTF_PACE_MIN_US 400u

/* Emit a compact CDC ring status line every N report lines so the exact byte
 * where output stops can be correlated with ring occupancy and drop count. */
#define BENCH_CDC_PROBE_EVERY 12u

extern void hw_udelay(uint32_t usec);
extern int vprintf(const char *fmt, __builtin_va_list ap);

static void bench_paced_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);

    uint32_t us = (n > 0) ? ((uint32_t)n * BENCH_PRINTF_PACE_US_PER_BYTE)
                          : BENCH_PRINTF_PACE_MIN_US;
    if (us < BENCH_PRINTF_PACE_MIN_US) {
        us = BENCH_PRINTF_PACE_MIN_US;
    }
    hw_udelay(us);

    /* Periodic ring telemetry. Formatted with snprintf and pushed straight into
     * the CDC ring with mcu_cdc_write(), bypassing printf entirely -- printf is
     * #defined to bench_paced_printf below, so calling it here would recurse and
     * the marker would also perturb the very counters it reports. */
    static unsigned int report_line;
    if ((++report_line % BENCH_CDC_PROBE_EVERY) == 0) {
        char m[96];
        int k = snprintf(m, sizeof(m),
                         "  <CDC line=%u dtr=%u pending=%u dropped=%u>\n",
                         report_line,
                         (unsigned int)(mcu_cdc_is_connected() ? 1u : 0u),
                         mcu_cdc_get_tx_pending(),
                         mcu_cdc_get_dropped_bytes());
        if (k > 0) {
            mcu_cdc_write(m, (size_t)((k < (int)sizeof(m)) ? k : (int)sizeof(m) - 1));
        }
    }
}

#define printf bench_paced_printf

/* Verified working pre-scheduler (self-test 5 uses it) and driven by Timer0,
 * so pacing the report cannot perturb the JL_TIMER5 measurement timebase. */
extern void hw_udelay(uint32_t usec);

/*
 * Watchdog service.
 *
 * The only wdt_clear() call site in the whole firmware is vApplicationIdleHook,
 * which is the FreeRTOS idle task. bench_run() executes at mcu_main.c:434,
 * before vTaskStartScheduler() at :498, so the entire benchmark runs with the
 * watchdog armed and never fed.
 *
 * That was survivable while bench_run took ~93 ms, but the report pacing added
 * ~131 ms of hw_udelay, taking the total to ~224 ms -- past the 128 ms WDT
 * timeout. A spontaneous restart was observed only after pacing was added, and
 * never before it, which fits: the pacing fix for the UART overrun is what
 * pushed the run over the threshold.
 *
 * wdt_clear() is called between measurement chunks and between paced output
 * lines, never inside a timed region, so it cannot perturb a measurement.
 */
extern void wdt_clear(void);

/* ------------------------------------------------------------------ */
/* Deferred output                                                      */
/* ------------------------------------------------------------------ */
/*
 * Every printf below is redirected into a RAM buffer and emitted as a single
 * burst at the end of bench_run().
 *
 * Output: paced printf(), one call per line.
 *
 * This replaced a private buffer + putchar() flush that was built up over
 * several rounds of guessing. The ADC, pad-strike and aftertouch logs all use
 * plain printf per line and are reliable, so the benchmark now uses the same
 * mechanism -- with pacing, because a back-to-back report overruns the SDK's
 * printf output budget (see bench_paced_printf below).
 *
 * What DID genuinely fault was an indirect variadic call: printf bound to a
 * function pointer and invoked as printf("%.*s", n, p). Calling printf directly
 * is fine.
 *
 * The benchmark runs before the scheduler starts (bench_run at mcu_main.c:452,
 * vTaskStartScheduler at :559) on Core 0, so no task can preempt a timed region.
 * Core 1 is not even started until mcu_main.c:489, after the benchmark returns.
 */

/*
 * Mark a benchmark phase: feeds the WDT and emits a progress marker.
 *
 * Each phase is its own printf call, so markers appear at the instant they are
 * reached and stay in order relative to the report body.
 */
static void bench_stage(const char *name)
{
    wdt_clear();
    printf("[%s]\n", name);
}

/* ------------------------------------------------------------------ */
/* pi32v2 algebraic-syntax FP primitives                                */
/* ------------------------------------------------------------------ */
/*
 * pi32v2 is Blackfin-derived: FP ops are written as equations with an (f)
 * suffix, not as mnemonics. There is no `fmadd` mnemonic; the fused op is
 * spelled as an accumulating multiply. The assembler resolves "%0"/"%1"/"%2"
 * into register names inside the equation, so normal "r" constraints work.
 *
 * Guarded so the file still compiles for non-pi32v2 targets.
 */
#if defined(__PI32V2__)

/** a += b * c   -- fused multiply-add. */
#define PI32V2_FMA(a, b, c) \
    __asm__ volatile("%0 += %1 * %2 (f)" : "+r"(a) : "r"(b), "r"(c))

/** a -= b * c   -- fused multiply-subtract. */
#define PI32V2_FMS(a, b, c) \
    __asm__ volatile("%0 -= %1 * %2 (f)" : "+r"(a) : "r"(b), "r"(c))

/** a = b * c */
#define PI32V2_MUL(a, b, c) \
    __asm__ volatile("%0 = %1 * %2 (f)" : "=r"(a) : "r"(b), "r"(c))

/** a = a + b */
#define PI32V2_ADD(a, b) \
    __asm__ volatile("%0 = %0 + %1 (f)" : "+r"(a) : "r"(b))

/** a = a - b */
#define PI32V2_SUB(a, b) \
    __asm__ volatile("%0 = %0 - %1 (f)" : "+r"(a) : "r"(b))

/** a = a / b */
#define PI32V2_DIV(a, b) \
    __asm__ volatile("%0 = %0 / %1 (f)" : "+r"(a) : "r"(b))

#else /* !__PI32V2__ -- plain C equivalents, for compile portability */

#define PI32V2_FMA(a, b, c) ((a) += (b) * (c))
#define PI32V2_FMS(a, b, c) ((a) -= (b) * (c))
#define PI32V2_MUL(a, b, c) ((a) = (b) * (c))
#define PI32V2_ADD(a, b) ((a) += (b))
#define PI32V2_SUB(a, b) ((a) -= (b))
#define PI32V2_DIV(a, b) ((a) /= (b))

#endif /* __PI32V2__ */

/* ------------------------------------------------------------------ */
/* Timebase: JL_TIMER5 @ 24 MHz                                          */
/* ------------------------------------------------------------------ */


/** TIMER5 clock: the 24 MHz oscillator, independent of the core clock. */
#define BENCH_TICK_HZ        24000000u
/** Counter width before auto-reload: PRD = 0xFFFF -> wraps at 65536. */
#define BENCH_TICK_WRAP      65536u

/** Iterations per timed chunk. Must stay under BENCH_TICK_WRAP ticks even for
 *  the slowest body measured here (a libm tanhf call, ~15 ticks), so that a
 *  single delta is always wrap-free: 2048 * 15 = 30720 < 65536. */
#define BENCH_CHUNK          2048u

/** Total iterations per reported measurement. */
#define BENCH_ITERS          100000u

static void bench_timer_init(void)
{
    JL_TIMER5->CON = (1u << 14);              /* clear pending flag      */
    JL_TIMER5->CNT = 0;
    JL_TIMER5->PRD = 0xFFFFu;                 /* 16-bit auto-reload     */
    JL_TIMER5->CON = (0x2u << 2) | (1u << 0); /* 24 MHz, periodic, run  */
    __asm__ volatile("csync");
}

/** Current 16-bit tick count. */
static inline uint32_t bench_ticks(void)
{
    return JL_TIMER5->CNT & 0xFFFFu;
}

/* ------------------------------------------------------------------ */
/* Measurement core                                                     */
/* ------------------------------------------------------------------ */

/** A measured loop body: performs `count` iterations starting at `first`. */
typedef void (*bench_body_fn)(uint32_t first, uint32_t count);

/**
 * Time `body` over `total` iterations, chunked so no single 16-bit delta wraps.
 *
 * The callback receives (first, count) so its loop trip count is part of the
 * measured region, identical between the baseline and the test body.
 */
static uint32_t bench_measure(bench_body_fn body, uint32_t total)
{
    uint32_t ticks = 0;
    uint32_t done = 0;

    while (done < total) {
        uint32_t n = BENCH_CHUNK;
        if (n > total - done) {
            n = total - done;
        }
        uint32_t t0 = bench_ticks();
        body(done, n);
        ticks += (bench_ticks() - t0) & 0xFFFFu;
        done += n;
        wdt_clear(); /* outside the timed region; see bench.c header note */
    }
    return ticks;
}

/**
 * Print a measurement as raw ticks plus a ratio against a reference op.
 *
 * Two earlier bugs lived here, both of which made the report unusable:
 *
 * 1. Unit math. This computed ns_each = ns_total / iters (already nanoseconds)
 *    and then printed ns_each / 1000 as the integer part, as if the value were
 *    milliseconds. ns_each is under 1000 for every op measured here, so that
 *    division truncated to 0 and only the fractional part survived: a real
 *    21.9 ns/iter printed as "0.936 ns". The tick-to-ns conversion is now
 *    gone entirely -- see bench_primitives for why raw ticks are the honest
 *    unit to report.
 *
 * 2. Subtracting an "empty loop" baseline. That is unsound on this toolchain:
 *    the volatile asm barrier in body_baseline blocks unrolling, so the empty
 *    loop pays full loop overhead every iteration, while the FP bodies get
 *    unrolled and amortise their counter/branch cost. The baseline therefore
 *    measured *slower* than the ops it was meant to subtract from, which is
 *    what produced the impossible "MUL+ADD 0.020 ns" and "FMSUB 0.000 ns".
 *
 * Raw ticks with a ratio against a reference op is immune to both problems and
 * is all the FMA decision actually needs: the absolute tick rate cancels in the
 * ratio, so no clock assumption has to be trusted to compare FMA against
 * MUL+ADD. Ratio is printed as an integer x100 to avoid float printf.
 */
static void bench_report(const char *label, uint32_t ticks, uint32_t ref)
{
    uint32_t x100 = ref ? (uint32_t)(((uint64_t)ticks * 100u) / ref) : 0u;

    printf("  %-9s %lu  %lu.%02lux\n", label, (unsigned long)ticks,
           (unsigned long)(x100 / 100u), (unsigned long)(x100 % 100u));
}

/* ------------------------------------------------------------------ */
/* 0. Timebase self-test                                                */
/* ------------------------------------------------------------------ */
/*
 * Printed before anything is measured. If the timer were not running this
 * would read 0/0, which is how the previous local_timer_us_time() based
 * version failed silently.
 */
static void body_self_test(uint32_t first, uint32_t count)
{
    (void)first;
    volatile float sink = 0.0f;
    float acc = 1.0f;
    for (uint32_t i = 0; i < count; i++) {
        PI32V2_ADD(acc, 1e-9f);
    }
    sink = acc;
    (void)sink;
}

/* ------------------------------------------------------------------ */
/* 0b. Float divide cost on this core                                    */
/* ------------------------------------------------------------------ */
/*
 * Added after the IR3109 "fast reciprocal" optimisation turned out to be a
 * REGRESSION: replacing `f / df` with a two-step Newton-Raphson reciprocal made
 * the filter ~670 ticks/voice slower, which means a hardware divide is cheaper
 * than the sequence meant to replace it.
 *
 * That inverts a load-bearing assumption. Divisions were treated as expensive
 * software work worth eliminating anywhere they appeared -- 17 per voice-sample
 * in the VCF alone. If a divide is cheap, that entire optimisation category is
 * dead, and no reciprocal substitution should be attempted again without
 * measuring here first.
 *
 * Each body runs the same loop shape over the same count with a volatile sink,
 * so the numbers are comparable to each other. Dependent divisions (each
 * denominator derived from the previous result) are timed separately from
 * independent ones because a pipelined divider can overlap the latter but not
 * the former, and the VCF's `f / df` is on a dependency chain.
 */
static volatile float fdiv_sink;

/* IMPORTANT: every divisor below is derived from the loop counter so that it
 * varies per iteration.
 *
 * The first version of this probe used literal constants (1.7f, 2.0f) and was
 * worthless: the compiler constant-folded the entire Newton-Raphson reciprocal,
 * so "NR recip" measured nothing but a multiply and appeared to be FASTER than a
 * divide. That directly contradicted the observed regression in the real filter
 * (~670 ticks/voice slower), which is what exposed the bug. The MUL reference was
 * broken the same way: `acc*0.5f` folds to `acc *= 2.0f`.
 *
 * A varying divisor also matches the real code, where the VCF's divisor is df,
 * a freshly computed value each sample.
 */
static void body_fdiv_dep(uint32_t first, uint32_t count)
{
    float acc = (float)first + 1.5f;

    for (uint32_t i = 0; i < count; i++) {
        float d = 1.7f + (float)i * 1.0e-4f;
        acc = 3.0f + (acc / d);
    }
    fdiv_sink = acc;
}

static void body_fmul(uint32_t first, uint32_t count)
{
    float acc = (float)first + 1.5f;

    for (uint32_t i = 0; i < count; i++) {
        float d = 1.7f + (float)i * 1.0e-4f;
        acc = 3.0f + (acc * d);   /* op-for-op identical to DIV dep below */
    }
    fdiv_sink = acc;
}

/* Chain-depth probes.
 *
 * The VCF replaced 4 divides with 4 multiplies per sample and gained NOTHING
 * (5128 vs 5129 ticks), while adding ~6 ops to the chain (the NR reciprocal) cost
 * 672 ticks/voice. That asymmetry suggests the filter is LATENCY-bound on its
 * dependency chain rather than throughput-bound: removing ops that sit on the
 * chain shortens nothing, and lengthening it is immediately expensive.
 *
 * These two bodies are 4 and 8 dependent multiplies deep. If cost scales with
 * depth, every op on the chain costs full latency and the only way to speed the
 * filter up is to SHORTEN the chain -- which is a different strategy from
 * reducing operation count, and is the thing to check before optimising further.
 * If cost is flat in depth, the chain is being hidden and op count is what counts.
 */
static void body_chain4(uint32_t first, uint32_t count)
{
    float acc = (float)first + 1.5f;

    for (uint32_t i = 0; i < count; i++) {
        float d = 1.7f + (float)i * 1.0e-4f;
        acc = 3.0f + acc * 0.999f;
        acc = 3.0f + acc * 0.999f;
        acc = 3.0f + acc * 0.999f;
        acc = 3.0f + acc * 0.999f;
        acc = acc + d * 0.0f;
    }
    fdiv_sink = acc;
}

static void body_chain8(uint32_t first, uint32_t count)
{
    float acc = (float)first + 1.5f;

    for (uint32_t i = 0; i < count; i++) {
        float d = 1.7f + (float)i * 1.0e-4f;
        acc = 3.0f + acc * 0.999f;
        acc = 3.0f + acc * 0.999f;
        acc = 3.0f + acc * 0.999f;
        acc = 3.0f + acc * 0.999f;
        acc = 3.0f + acc * 0.999f;
        acc = 3.0f + acc * 0.999f;
        acc = 3.0f + acc * 0.999f;
        acc = 3.0f + acc * 0.999f;
        acc = acc + d * 0.0f;
    }
    fdiv_sink = acc;
}

/* The exact shape of the regression: two Newton-Raphson refinements over a
 * bit-level seed, i.e. what ir3109_fast_recip() costs. */
static inline float bench_fast_recip(float x)
{
    union { float f; uint32_t u; } v;

    v.f = x;
    v.u = 0x7EF311C3u - v.u;
    float y = v.f;
    y = y * (2.0f - x * y);
    return y * (2.0f - x * y);
}

static void body_frecip(uint32_t first, uint32_t count)
{
    float acc = (float)first + 1.5f;

    for (uint32_t i = 0; i < count; i++) {
        float d = 1.7f + (float)i * 1.0e-4f;
        acc = 3.0f + (acc * bench_fast_recip(d));
    }
    fdiv_sink = acc;
}

static void bench_fp_cost_probe(void)
{
    const uint32_t N = 20000u;
    uint32_t t, ref, tdiv, trecip, c4, c8;

    printf("\n-- fp cost, %lu iters, varying divisor --\n", (unsigned long)N);

    t = bench_measure(body_fmul, N);
    ref = t;
    bench_report("MUL", t, ref);

    tdiv = bench_measure(body_fdiv_dep, N);
    bench_report("DIV dep", tdiv, ref);

    trecip = bench_measure(body_frecip, N);
    bench_report("NR recip", trecip, ref);

    c4 = bench_measure(body_chain4, N);
    bench_report("chain 4mul", c4, ref);

    c8 = bench_measure(body_chain8, N);
    bench_report("chain 8mul", c8, ref);

    /* Decision 1: divide vs the substitution meant to replace it. Same loop,
     * same varying divisor, only the instruction differs. */
    if (trecip > tdiv) {
        printf("  => NR recip %lu%% SLOWER than divide: reciprocals are dead.\n",
               (unsigned long)(((uint64_t)(trecip - tdiv) * 100u) / tdiv));
    } else {
        printf("  => NR recip %lu%% of divide: reciprocal is viable.\n",
               (unsigned long)(((uint64_t)trecip * 100u) / tdiv));
    }

    /* Decision 2: does cost track chain depth? 8 mul deep vs 4 mul deep should
     * cost roughly 2x if each op pays full latency (latency-bound), or about the
     * same if the chain is hidden (throughput-bound).
     * NOTE: bench_report() divides its x100 by 100 for the integer part; this
     * verdict originally printed the raw x100, so a true 1.53x read as 153.53x. */
    uint32_t x100 = (uint32_t)(((uint64_t)c8 * 100u) / c4);
    printf("  => 8-deep vs 4-deep: %lu.%02lux  (%s)\n",
           (unsigned long)(x100 / 100u), (unsigned long)(x100 % 100u),
           (c8 > (c4 + (c4 / 4u))) ? "LATENCY-BOUND: shorten the chain"
                                   : "throughput-bound: cut op count");
}

static void bench_timer_probe(void)
{
    uint32_t a = bench_ticks();
    body_self_test(0, 100000u);
    uint32_t b = bench_ticks();
    uint32_t t = (b - a) & 0xFFFFu;

    /* Raw ticks are printed here (and only here) so a dead timebase is
     * unambiguous: a zero tick count means the timer is not counting, and
     * every ns figure below would be meaningless. */
    /* The us figure assumes the 24 MHz clock source; it is informational only and
     * every decision below is made from tick ratios, which do not depend on it. */
    printf("TIMEBASE JL_TIMER5@24MHz %s  100k-adds=%lu ticks (%lu us)\n",
           t ? "OK" : "** NOT COUNTING **",
           (unsigned long)t, (unsigned long)(t / 24u));
}

/* ------------------------------------------------------------------ */
/* 1. Fused multiply-add correctness                                    */
/* ------------------------------------------------------------------ */

static float bench_b, bench_c;

/* True empty baseline: the same loop shape with no FP work at all, so a
 * measured op's full cost is reported rather than only its difference from
 * another FP op. The earlier version of this body did one PI32V2_ADD against
 * an immediate, which made it a second FP measurement rather than a baseline
 * and understated every per-op figure by one add.
 *
 * The volatile asm with no operands is a compiler barrier: it keeps the loop
 * from being hoisted or collapsed while emitting nothing on this ISA. */
static void body_baseline(uint32_t first, uint32_t count)
{
    (void)first;
    uint32_t k = 0;
    for (uint32_t i = 0; i < count; i++) {
        __asm__ volatile("" : "+r"(k));
    }
    if (k == 0xDEADBEEFu) {
        printf(" ");
    }
}

static void body_fma(uint32_t first, uint32_t count)
{
    (void)first;
    float acc = 0.5f;
    for (uint32_t i = 0; i < count; i++) {
        PI32V2_FMA(acc, bench_b, bench_c);
    }
    if (acc == 12345.678f) {
        printf(" ");
    }
}

static void body_muladd(uint32_t first, uint32_t count)
{
    (void)first;
    float acc = 0.5f;
    for (uint32_t i = 0; i < count; i++) {
        float p;
        PI32V2_MUL(p, bench_b, bench_c);
        PI32V2_ADD(acc, p);
    }
    if (acc == 12345.678f) {
        printf(" ");
    }
}

static int bench_fma_correct(void)
{
    /* Value table is exercised for exactness against a separately-rounded C
     * reference, but the per-case dump is suppressed: it consumed ~600 bytes
     * of a ~2 KB capture budget. Only a pass count is reported. */
    static const float av[] = { 1.0f, -2.5f, 3.0f, 1e6f, 0.1f, -0.0f, 7.0f };
    static const float bv[] = { 2.0f, 4.0f, -1.5f, 1e-3f, 3.0f, 5.0f, -0.25f };
    static const float cv[] = { 3.0f, 2.0f, 0.5f, -1e3f, -0.3f, 1.0f, 2.0f };
    const int n = (int)(sizeof(av) / sizeof(av[0]));
    int ok = 0;

    for (int i = 0; i < n; i++) {
        float got = av[i];
        PI32V2_FMA(got, bv[i], cv[i]);
        if (got == av[i] + bv[i] * cv[i]) {
            ok++;
        }
    }
    printf("FMA value test %d/%d exact vs a+b*c\n", ok, n);
    if (ok != n) {
        printf("FMA RESULT: ** WRONG **\n");
        return 0;
    }

    /* True-FMA probe: does the fused slot round once or twice?
     *
     * These operands are load-bearing, so do not "simplify" them. For
     * a=0.2, b=0.3, c=0.2 the product b*c is not representable in float32, so:
     *   single rounding (real FMA) -> 0.2600000203
     *   double rounding (mul+add)  -> 0.2599999905
     * which is exactly 1 ulp apart (ulp = 2^-25 = 2.98e-8 at this magnitude).
     *
     * Two earlier probes were wrong and had to be replaced:
     *   (1.0, 1+2e-7, 1-2e-7) - the product rounds to exactly 1.0f, so both
     *     paths agree and the probe cannot distinguish anything.
     *   (0.5, 0.3, 0.3)        - computed with double-precision operands; with
     *     genuine float32 operands both paths give 0.5900000334.
     */
    {
        static const struct { float a, b, c, fused, split; } probes[] = {
            { 0.2f, 0.3f, 0.2f, 0.2600000203f, 0.2599999905f },
            { 0.9f, 3.0f, 0.1f, 1.199999928f,  1.200000048f  },
        };
        const unsigned np = sizeof(probes) / sizeof(probes[0]);
        unsigned single = 0;

        for (unsigned i = 0; i < np; i++) {
            float fused = probes[i].a;
            PI32V2_FMA(fused, probes[i].b, probes[i].c);
            if (fused == probes[i].fused) {
                single++;
            }
        }
        printf("FMA 1-ulp probe %u/%u round ONCE (true fused)\n", single, np);
        if (single) {
            printf("FMA ROUNDING: SINGLE -> true IEEE FMA\n");
        } else {
            float fused = probes[0].a;
            PI32V2_FMA(fused, probes[0].b, probes[0].c);
            printf("FMA ROUNDING: DOUBLE (got %.9g, want %.9g)\n",
                   (double)fused, (double)probes[0].fused);
            printf("FMA IS A DROP-IN FOR a+b*c (bit-exact, no rounding change)\n");
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* 2. FP primitive costs                                                */
/* ------------------------------------------------------------------ */

static void body_add(uint32_t first, uint32_t count)
{
    (void)first;
    float acc = 0.5f;
    for (uint32_t i = 0; i < count; i++) {
        PI32V2_ADD(acc, bench_b);
    }
    if (acc == 12345.678f) {
        printf(" ");
    }
}

static void body_sub(uint32_t first, uint32_t count)
{
    (void)first;
    float acc = 0.5f;
    for (uint32_t i = 0; i < count; i++) {
        PI32V2_SUB(acc, bench_b);
    }
    if (acc == 12345.678f) {
        printf(" ");
    }
}

static void body_mul(uint32_t first, uint32_t count)
{
    (void)first;
    float acc = 0.5f;
    for (uint32_t i = 0; i < count; i++) {
        PI32V2_MUL(acc, acc, bench_c);   /* dependent: feeds the next iteration */
    }
    if (acc == 12345.678f) {
        printf(" ");
    }
}

static void body_fms(uint32_t first, uint32_t count)
{
    (void)first;
    float acc = 0.5f;
    for (uint32_t i = 0; i < count; i++) {
        PI32V2_FMS(acc, bench_b, bench_c);
    }
    if (acc == 12345.678f) {
        printf(" ");
    }
}

static void body_div(uint32_t first, uint32_t count)
{
    (void)first;
    float acc = 0.5f;
    for (uint32_t i = 0; i < count; i++) {
        PI32V2_DIV(acc, bench_c);
    }
    if (acc == 12345.678f) {
        printf(" ");
    }
}

/* Four independent accumulators: hides latency, exposes throughput. */
static void body_fma_x4(uint32_t first, uint32_t count)
{
    (void)first;
    float a0 = 0.5f, a1 = 0.25f, a2 = 0.125f, a3 = 0.0625f;
    for (uint32_t i = 0; i < count; i++) {
        PI32V2_FMA(a0, bench_b, bench_c);
        PI32V2_FMA(a1, bench_b, bench_c);
        PI32V2_FMA(a2, bench_b, bench_c);
        PI32V2_FMA(a3, bench_b, bench_c);
    }
    if (a0 + a1 + a2 + a3 == 12345.678f) {
        printf(" ");
    }
}

static void body_muladd_x4(uint32_t first, uint32_t count)
{
    (void)first;
    float a0 = 0.5f, a1 = 0.25f, a2 = 0.125f, a3 = 0.0625f;
    for (uint32_t i = 0; i < count; i++) {
        float p;
        PI32V2_MUL(p, bench_b, bench_c); PI32V2_ADD(a0, p);
        PI32V2_MUL(p, bench_b, bench_c); PI32V2_ADD(a1, p);
        PI32V2_MUL(p, bench_b, bench_c); PI32V2_ADD(a2, p);
        PI32V2_MUL(p, bench_b, bench_c); PI32V2_ADD(a3, p);
    }
    if (a0 + a1 + a2 + a3 == 12345.678f) {
        printf(" ");
    }
}

__attribute__((unused)) static void bench_primitives(void)
{
    /* Raw ticks per BENCH_ITERS, ratio relative to FMUL. No ns conversion and
     * no baseline subtraction -- see bench_report for why. A ratio of 1.00x
     * against MUL+ADD is the whole question: equal means no reason to change
     * the DSP code, lower means the fused slot is worth adopting. */
    printf("-- ticks per %lu iters, ratio vs FMUL --\n",
           (unsigned long)BENCH_ITERS);

    bench_b = 1.0001f;
    bench_c = 0.9999f;

    uint32_t ref = bench_measure(body_mul, BENCH_ITERS);
    printf("  %-9s %lu  1.00x  (reference)\n", "FMUL", (unsigned long)ref);

    bench_report("FADD",   bench_measure(body_add, BENCH_ITERS),    ref);
    bench_report("FSUB",   bench_measure(body_sub, BENCH_ITERS),    ref);
    bench_report("FMA",    bench_measure(body_fma, BENCH_ITERS),    ref);
    bench_report("FMSUB",  bench_measure(body_fms, BENCH_ITERS),    ref);
    bench_report("MUL+ADD", bench_measure(body_muladd, BENCH_ITERS), ref);
    bench_report("FDIV",   bench_measure(body_div, BENCH_ITERS),    ref);

    /* Printed for diagnosis only: this is NOT a valid baseline. A value above
     * 1.00x here just confirms the empty loop is dominated by loop overhead. */
    bench_report("empty*", bench_measure(body_baseline, BENCH_ITERS), ref);

    printf("-- 4 indep chains, ticks per op (per-op, not per-loop) --\n");
    bench_report("FMAx4",    bench_measure(body_fma_x4, BENCH_ITERS) / 4u, ref);
    bench_report("MULADDx4", bench_measure(body_muladd_x4, BENCH_ITERS) / 4u, ref);
}

/* ------------------------------------------------------------------ */
/* 3. tanh: libm versus polynomial                                      */
/* ------------------------------------------------------------------ */
/*
 * chorus_process() applies tanhf(x * 0.12f) / 0.12f to both channels every
 * sample: 96,000 libm calls per second at 48 kHz. Each candidate is timed and
 * its worst-case error reported against libm tanhf over the argument range the
 * chorus can actually produce, so the accuracy/speed tradeoff is explicit.
 */

/** Odd series, exact to x^7. Accurate only for small |x|. */
static float tanh_poly7(float x)
{
    float x2 = x * x;
    float p = -0.33333334f + x2 * (0.13333334f + x2 * -0.053968254f);
    return x + x * (x2 * p);
}

/** Odd series, exact to x^9. */
static float tanh_poly9(float x)
{
    float x2 = x * x;
    float p = -0.33333334f
            + x2 * (0.13333334f
            + x2 * (-0.053968254f + x2 * -0.021869489f));
    return x + x * (x2 * p);
}

/** Saturating rational, same 3rd-order Pade the IR3109 model uses. */
static float tanh_pade(float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return (x * (27.0f + x2)) / (27.0f + 9.0f * x2);
}

static float tanh_err(float (*fn)(float), float lo, float hi, uint32_t steps)
{
    float worst = 0.0f;
    for (uint32_t i = 0; i <= steps; i++) {
        float x = lo + (hi - lo) * ((float)i / (float)steps);
        float e = fabsf(fn(x) - tanhf(x));
        if (e > worst) {
            worst = e;
        }
    }
    return worst;
}

static float bench_x;

/** Shared loop skeleton so the baseline carries identical bookkeeping. */
static float tanh_bench_ticks(float (*fn)(float), uint32_t iters, int exact_div)
{
    const float drive = 0.12f;
    uint32_t ticks = 0;
    uint32_t done = 0;
    volatile float sink = 0.0f;

    bench_x = 0.05f;
    while (done < iters) {
        uint32_t n = BENCH_CHUNK;
        if (n > iters - done) {
            n = iters - done;
        }
        uint32_t t0 = bench_ticks();
        for (uint32_t i = 0; i < n; i++) {
            if (fn) {
                if (exact_div) {
                    sink += fn(bench_x * drive) / drive;
                } else {
                    sink += fn(bench_x * drive) * (1.0f / drive);
                }
            } else {
                sink += bench_x * drive;      /* baseline: no tanh at all */
            }
            bench_x = bench_x * 1.0000001f + 1e-9f;
        }
        ticks += (bench_ticks() - t0) & 0xFFFFu;
        done += n;
    }
    (void)sink;
    return (float)ticks;
}

__attribute__((unused)) static void bench_tanh(void)
{
    enum { CALLS = 20000u, SWEEP = 2000u };

    /* Raw ticks per CALLS. The harness line covers the loop, the function
     * pointer call and the divide, so treat it as overhead, not as a baseline
     * to subtract -- the same reasoning as the empty-loop case above. */
    printf("-- tanh: ticks per %u calls (libm reference) --\n", CALLS);

    float t_base = tanh_bench_ticks(NULL, CALLS, 0);
    uint32_t ref = (uint32_t)tanh_bench_ticks(tanhf, CALLS, 1);
    printf("  %-9s %lu  1.00x  (reference)\n", "tanhf", (unsigned long)ref);

    bench_report("harness", (uint32_t)t_base, ref);
    bench_report("poly7",
                 (uint32_t)(tanh_bench_ticks(tanh_poly7, CALLS, 1) - t_base), ref);
    bench_report("poly9",
                 (uint32_t)(tanh_bench_ticks(tanh_poly9, CALLS, 1) - t_base), ref);
    bench_report("pade",
                 (uint32_t)(tanh_bench_ticks(tanh_pade, CALLS, 1) - t_base), ref);

    /* Isolate the divide: the chorus expression divides by the 0.12 drive on
     * every sample, which is a real FDIV. Multiplying by the exact reciprocal
     * is not bit-identical in general, so measure both paths separately.
     * (Both used to pass exact_div=1, which made the two lines identical and
     * the comparison meaningless.) */
    bench_report("/drive",
                 (uint32_t)(tanh_bench_ticks(tanhf, CALLS, 1) - t_base), ref);
    bench_report("*recip",
                 (uint32_t)(tanh_bench_ticks(tanhf, CALLS, 0) - t_base), ref);

    /* Accuracy of the reciprocal substitution, so the speed win can be weighed
     * against a measured error rather than assumed free. */
    {
        const float drive = 0.12f;
        const float recip = 1.0f / drive;
        float worst = 0.0f;
        for (uint32_t i = 0; i <= SWEEP; i++) {
            float x = -0.41f + 0.82f * ((float)i / (float)SWEEP);
            float t = tanhf(x);
            float d = fabsf(t * recip - t / drive);
            if (d > worst) {
                worst = d;
            }
        }
        printf("  recip-vs-div max abs diff = %.4e\n", (double)worst);
    }

    printf("-- worst-case abs err vs libm tanhf over the chorus range --\n");

    struct { const char *name; float (*fn)(float); } cands[] = {
        { "poly7", tanh_poly7 },
        { "poly9", tanh_poly9 },
        { "pade",  tanh_pade  },
    };

    for (unsigned i = 0; i < sizeof(cands) / sizeof(cands[0]); i++) {
        float e = tanh_err(cands[i].fn, -0.41f, 0.41f, SWEEP);
        printf("  %-6s abs err = %.4e   (f32 eps 1.19e-07)\n",
               cands[i].name, (double)e);
    }
}

/* ------------------------------------------------------------------ */

/*
 * Marginal cost of one more voice.
 *
 * The FP primitive numbers elsewhere in this file do not predict polyphony on
 * their own: the question is how many voices fit in one block period. This
 * drives the real per-voice path (voice_update_control_block once per block,
 * then voice_process_sample per sample) at 1..JUNO_NUM_VOICES voices and reports
 * the marginal cost of each added voice, which is exactly what a voice-count
 * increase has to pay.
 *
 * Deliberately excludes the chorus and master HPF. Those run once per block as
 * global mono->stereo stages and do not scale with voice count, so they cannot
 * affect polyphony; including them would only dilute the per-voice figure.
 * A JunoEngine cannot be instantiated here either -- its chorus delay buffers
 * are 2x4096 floats and the app already heap-allocates one.
 *
 * Budget: one 128-sample block at 48 kHz is 128/48000 s = 2.667 ms and
 * JL_TIMER5 counts at 24 MHz, so a block period is 64000 ticks. Expressed as
 * tick-rate * samples-per-block / sample-rate; multiplying the rates together
 * instead overflows 32 bits and the out-of-range float-to-uint cast folds to 1.
 * 100% means the voice path alone consumes the entire period, leaving nothing
 * for the rest of the audio task.
 */
#define BENCH_JUNO_SR        48000.0f
#define BENCH_JUNO_BLOCK     128
#define BENCH_JUNO_SUB       32
/* synth_get_samples() renders the Juno engine in 32-sample chunks, so all
 * block-rate work is charged at that granularity in the full-engine sweep. */
#define BENCH_JUNO_CHUNK     32
#define BENCH_JUNO_BLOCK_TICKS ((uint32_t)(((float)BENCH_TICK_HZ * (float)BENCH_JUNO_BLOCK) \
                                            / BENCH_JUNO_SR))

__attribute__((unused)) /* -------------------------------------------------------------------------
 * Per-voice component ablation.
 *
 * The full-engine sweep shows ~12.8k ticks per voice but not what is inside it.
 * Six voices is 145% of a 128-sample block, so the optimisation target has to be
 * chosen from measurements, not intuition. Each case disables exactly one part of
 * the heavy patch and reports the saving versus the heavy baseline, so the cost
 * of every oscillator and of the filter modulation is separable.
 *
 * Timed with a single voice over one 128-sample block, in 32-sample sub-chunks so
 * no delta can approach the 16-bit timer wrap. The voice card is re-initialised
 * per case so filter and envelope state cannot leak between measurements.
 * ------------------------------------------------------------------------- */
static volatile float ablate_sink;

static uint32_t bench_time_one_voice(const JunoEngineParams *p)
{
    static JunoVoiceCard v;
    static VoiceTolerance tol;

    voice_init(&v, 0, &tol, BENCH_JUNO_SR);
    voice_note_on(&v, 48, 0.9f, 0.0f);

    uint32_t total = 0;
    for (int sub = 0; sub < BENCH_JUNO_BLOCK / BENCH_JUNO_SUB; sub++) {
        uint32_t t0 = bench_ticks();
        for (int s = 0; s < BENCH_JUNO_SUB; s++) {
            ablate_sink += voice_process_sample(&v, p, 0.0f, 1.0f, 0.5f);
        }
        total += (bench_ticks() - t0) & 0xFFFFu;
        wdt_clear(); /* outside the timed region */
    }
    return total;
}

static void bench_voice_components(void)
{
    const JunoEngineParams *heavy =
        (const JunoEngineParams *)synth_bench_juno_heavy_params();
    JunoEngineParams p;

    printf("\n-- per-voice ablation, 1 voice, %d samples --\n", BENCH_JUNO_BLOCK);

    uint32_t base = bench_time_one_voice(heavy);
    printf("  heavy baseline   %6lu\n", (unsigned long)base);

    /* Signed difference: these cases can land a tick or two ABOVE the baseline
     * from code-layout luck, which underflows an unsigned subtraction and prints
     * as 4294967295. Report that honestly instead. */
    #define BENCH_SAVES(c) ((long)(base) - (long)(c))

    p = *heavy;  p.dco_noise = 0.0f;
    uint32_t c = bench_time_one_voice(&p);
    printf("  -noise           %6lu  saves %ld\n",
           (unsigned long)c, BENCH_SAVES(c));

    p = *heavy;  p.dco_sub = false; p.dco_sub_level = 0.0f;
    c = bench_time_one_voice(&p);
    printf("  -sub             %6lu  saves %ld\n",
           (unsigned long)c, BENCH_SAVES(c));

    p = *heavy;  p.dco_pulse = false;
    c = bench_time_one_voice(&p);
    printf("  -pulse           %6lu  saves %ld\n",
           (unsigned long)c, BENCH_SAVES(c));

    p = *heavy;  p.dco_pwm = 0.0f; p.dco_pwm_mode = 0;
    c = bench_time_one_voice(&p);
    printf("  -pwm LFO         %6lu  saves %ld\n",
           (unsigned long)c, BENCH_SAVES(c));

    p = *heavy;  p.vcf_env_mod = 0.0f; p.vcf_lfo_mod = 0.0f;
    p.vcf_kbd_track = 0.0f; p.vcf_res = 0.0f;
    c = bench_time_one_voice(&p);
    printf("  -vcf mod/res     %6lu  saves %ld\n",
           (unsigned long)c, BENCH_SAVES(c));

    p = *heavy;  p.dco_noise = 0.0f; p.dco_sub = false; p.dco_sub_level = 0.0f;
    p.dco_pulse = false; p.dco_pwm = 0.0f; p.dco_pwm_mode = 0;
    c = bench_time_one_voice(&p);
    printf("  saw only         %6lu  saves %ld\n",
           (unsigned long)c, BENCH_SAVES(c));
    #undef BENCH_SAVES

    /* Direct measurement of the IR3109 filter itself.
     *
     * None of the cases above size the VCF: they only remove modulation
     * arithmetic, while the filter runs per sample regardless. Adding a bypass
     * branch to voice_process_sample() would contaminate every other row, so
     * instead we call vcf_process() directly on the same voice state the
     * ablation uses and time it over the same window.
     *
     * Caveat: driven by a 110 Hz saw at unity rather than the real DCO mix, and
     * coefficients come from voice_update_control_block() on the heavy patch.
     * Treat as an upper-bound estimate of the filter's per-sample cost. */
    {
        static JunoVoiceCard v;
        static VoiceTolerance tol;
        voice_init(&v, 0, &tol, BENCH_JUNO_SR);
        voice_note_on(&v, 48, 0.9f, 0.0f);
        voice_update_control_block(&v, heavy, 0.0f, 1.0f, 0.0f);

        uint32_t vt = 0;
        for (int sub = 0; sub < BENCH_JUNO_BLOCK / BENCH_JUNO_SUB; sub++) {
            uint32_t t0 = bench_ticks();
            for (int s = 0; s < BENCH_JUNO_SUB; s++) {
                float ph = (float)(sub * BENCH_JUNO_SUB + s) / (float)BENCH_JUNO_SR;
                ph = ph * 110.0f;
                ph = ph - (float)(int32_t)ph;   /* 110 Hz sawtooth, no libm */
                ablate_sink += vcf_process(&v.vcf, ph * 2.0f - 1.0f);
            }
            vt += (bench_ticks() - t0) & 0xFFFFu;
            wdt_clear();
        }
        printf("  [vcf alone]      %6lu  = %lu%% of heavy\n",
               (unsigned long)vt, (unsigned long)(vt * 100u / base));
    }
}

__attribute__((unused)) static void bench_juno_voices(void)
{
    static JunoVoiceCard voice_cards[JUNO_NUM_VOICES];
    static VoiceTolerance voice_tol;
    /* Heavy patch, not the factory default: bank0/patch0 is a plain
     * single-oscillator patch that leaves sub, noise, PWM and the chorus idle,
     * so it understates the per-voice cost. */
    const JunoEngineParams *p = (const JunoEngineParams *)synth_bench_juno_heavy_params();

    for (int v = 0; v < JUNO_NUM_VOICES; v++) {
        voice_init(&voice_cards[v], v, &voice_tol, BENCH_JUNO_SR);
    }

    printf("\n-- juno voice path HEAVY patch, %d-sample block, budget %lu --\n",
           BENCH_JUNO_BLOCK, (unsigned long)BENCH_JUNO_BLOCK_TICKS);

    float sink = 0.0f;
    uint32_t prev = 0;
    for (int n = 1; n <= JUNO_NUM_VOICES; n++) {
        for (int v = 0; v < n; v++) {
            voice_note_on(&voice_cards[v], 48 + v, 0.9f, 0.0f);
        }

        /* bench_ticks() is only 16 bits wide (JL_TIMER5->PRD is 0xFFFF), so a
         * single delta spanning more than 65535 ticks wraps silently and yields
         * garbage. Accumulate in 32-sample sub-chunks -- about 15k ticks even at
         * 6 voices -- and sum masked deltas, exactly as bench_measure() does. */
        uint32_t total = 0;
        for (int sub = 0; sub < BENCH_JUNO_BLOCK / BENCH_JUNO_SUB; sub++) {
            uint32_t t0 = bench_ticks();
            for (int s = 0; s < BENCH_JUNO_SUB; s++) {
                for (int v = 0; v < n; v++) {
                    sink += voice_process_sample(&voice_cards[v], p, 0.0f, 1.0f, 0.5f);
                }
            }
            total += (bench_ticks() - t0) & 0xFFFFu;
            wdt_clear(); /* outside the timed region */
        }
        uint32_t per = total;

        printf("  voices=%d %6lu ticks  %3lu%% of budget  (+%lu per voice)\n",
               n, (unsigned long)per,
               (unsigned long)((per * 100u) / BENCH_JUNO_BLOCK_TICKS),
               (unsigned long)(per - prev));
        prev = per;

        for (int v = 0; v < n; v++) {
            voice_note_off(&voice_cards[v]);
        }
    }

    /* Keep the accumulated samples alive so the loop cannot be optimised out. */
    if (sink == 12345.678f) {
        printf(" ");
    }
}

/*
 * Accuracy of a division-light rewrite of the IR3109 nonlinear stage.
 *
 * vcf_nl_stage() currently costs four divides per stage (rational_soft_clip,
 * /ota_scale, ota_deriv, f/df), and x2 = sd*sd is computed twice. Restructuring
 * to one shared reciprocal r = 1/(27+9*x2) turns that into two divides (r and
 * f/df) and drops the duplicate x2. That is the single largest saving
 * available -- the voice path needs ~95% of a block period at 6 voices -- but
 * multiplying by a reciprocal is NOT bit-exact, and this sits inside a
 * recursive feedback path where error can accumulate.
 *
 * So measure the error before adopting it. Both formulations are reproduced
 * here standalone; production code is untouched. Note the clamps: the original
 * returns +/-1 from rational_soft_clip and 0 from ota_deriv for |x| >= 3, so
 * sweeping must respect them or the comparison is meaningless.
 */
__attribute__((unused)) static void bench_vcf_recip_error(void)
{
    const float g = 0.55f;
    const float ota_scale = 3.0f;
    const float inv_ota_scale = 1.0f / ota_scale;

    float maxDt = 0.0f;
    float maxDd = 0.0f;
    float maxRt = 0.0f;
    float maxRd = 0.0f;

    for (int i = -4000; i <= 4000; i++) {
        float sd = (float)i * 0.001f; /* -4..4, crosses the |x|>=3 clamps */

        /* --- current formulation (as in polyblep.h / ir3109_vcf.c) --- */
        float t_old, df_old;
        if (sd >= 3.0f || sd <= -3.0f) {
            t_old = (sd > 0.0f ? 1.0f : -1.0f) / ota_scale;
            df_old = 1.0f;
        } else {
            float x2 = sd * sd;
            t_old = ((sd * (27.0f + x2)) / (27.0f + 9.0f * x2)) / ota_scale;
            float d = 27.0f + 9.0f * x2;
            df_old = 1.0f + g * ((27.0f * (27.0f - 3.0f * x2)) / (d * d));
        }

        /* --- candidate: one shared reciprocal, no duplicate x2 --- */
        float t_new, df_new;
        if (sd >= 3.0f || sd <= -3.0f) {
            t_new = (sd > 0.0f ? 1.0f : -1.0f) * inv_ota_scale;
            df_new = 1.0f;
        } else {
            float x2 = sd * sd;
            float r = 1.0f / (27.0f + 9.0f * x2);
            t_new = sd * (27.0f + x2) * r * inv_ota_scale;
            df_new = 1.0f + (g * 27.0f * (27.0f - 3.0f * x2)) * r * r;
        }

        float dt = fabsf(t_new - t_old);
        float dd = fabsf(df_new - df_old);
        float at = fabsf(t_old) > 1e-12f ? dt / fabsf(t_old) : dt;
        float ad = fabsf(df_old) > 1e-12f ? dd / fabsf(df_old) : dd;
        if (dt > maxDt) maxDt = dt;
        if (dd > maxDd) maxDd = dd;
        if (at > maxRt) maxRt = at;
        if (ad > maxRd) maxRd = ad;
    }

    printf("\n-- vcf reciprocal rewrite: error over sd in [-4,4] (g=%.2f ota_scale=%.2f) --\n",
           g, ota_scale);
    printf("   t  (saturation)  max abs %.3e   max rel %.3e\n", maxDt, maxRt);
    printf("   df (derivative)  max abs %.3e   max rel %.3e\n", maxDd, maxRd);
    printf("   (float32 eps = %.3e; 1 ulp near 1.0 = %.3e)\n",
           1.1920929e-07f, 1.1920929e-07f);
    wdt_clear();
}

/*
 * Full-engine block cost, through the real juno_engine_process_block() entry
 * point the app uses.
 *
 * bench_juno_voices() times voice_process_sample() alone, so it excludes the
 * chorus, master HPF, output conversion and per-block overhead. At 6 voices
 * that path alone is ~95% of a 128-sample block (64000 ticks), which leaves
 * ~3000 ticks for everything else combined -- so the fixed cost is the number
 * that decides whether 6 voices is reachable at all.
 *
 * Renders in 32-sample chunks because synth_get_samples() does, so the per-chunk
 * block-rate work (voice_update_control_block, chorus control, HPF coefficients)
 * is charged at its real frequency. Measured in 32-sample chunks so no delta can
 * approach the 16-bit timer wrap.
 */
/* Returns the previous sweep point's tick count in *prev so the caller can show
 * a marginal per-voice cost without re-timing. Prints one line only, to keep the
 * shared boot-log budget intact. */
static int bench_juno_full_quiet(int voices, unsigned int *prev)
{
    float out_l[32];
    float out_r[32];

    synth_bench_juno_set_voices(voices);
    wdt_clear();

    unsigned int init_done = 0, juno = 0, dexed = 0, active = 0;
    synth_bench_juno_status(&init_done, &juno, &dexed, &active);
    if (juno == 0u || active == 0u) {
        return 0;
    }

    uint32_t total = 0;
    for (int blk = 0; blk < BENCH_JUNO_BLOCK / BENCH_JUNO_CHUNK; blk++) {
        uint32_t t0 = bench_ticks();
        synth_bench_juno_render(out_l, out_r, BENCH_JUNO_CHUNK);
        total += (bench_ticks() - t0) & 0xFFFFu;
        wdt_clear();
    }

    unsigned int cur = (unsigned int)total;
    printf("  voices=%d  %6u ticks  %3u%%  active=%u  (+%u/voice)\n",
           voices, cur, (cur * 100u) / BENCH_JUNO_BLOCK_TICKS, active,
           (voices > 1) ? (cur - *prev) : cur);
    *prev = cur;
    return 1;
}

/* Report the live engine's parameter set, so a sweep line can be attributed to
 * a patch even when the patch was installed by name rather than by the
 * synthetic worst-case hook. One short line; the boot log shares a ~2 KB
 * printf budget and this sweep is not the place to spend it. */
static void bench_juno_report_patch(SynthBenchJunoPatch which)
{
    unsigned int init_done = 0, juno = 0, dexed = 0, active = 0;
    synth_bench_juno_status(&init_done, &juno, &dexed, &active);

    if (juno == 0u) {
        printf("  UNAVAILABLE synth_init=%u s_dexed=0x%08x s_juno=0x%08x\n",
               init_done, dexed, juno);
        return;
    }
    /* Take bank/index from the id being reported. Passing a label string and
     * reading NUMERICAL's indices instead would print the wrong location for
     * every preset except the first. */
    printf("  patch=%s bank=%u/%u\n", synth_bench_juno_patch_name(which),
           synth_bench_juno_patch_bank(which),
           synth_bench_juno_patch_index(which));
}

/* One 1..6 sweep against whatever patch is currently installed. */
static void bench_juno_sweep_voices(void)
{
    unsigned int prev = 0;
    for (int n = 1; n <= 6; n++) {
        bench_juno_full_quiet(n, &prev);
    }
}

void bench_juno_full_sweep(void)
{
    bench_cdc_diag("pre-sweep");

    /* Availability is checked before installing anything: if the engine does not
     * exist, installing a patch is a silent no-op and the sweep would report
     * timings for whatever happened to be there, which reads as real data. */
    synth_bench_juno_set_voices(0);
    unsigned int init_done = 0, juno = 0, dexed = 0, active = 0;
    synth_bench_juno_status(&init_done, &juno, &dexed, &active);
    if (juno == 0u) {
        printf("\n-- full engine sweep --\n");
        printf("  UNAVAILABLE synth_init=%u s_dexed=0x%08x s_juno=0x%08x\n",
               init_done, dexed, juno);
        return;
    }

    /* Patch 1: the synthetic worst case. Still the reference for "does N voices
     * fit in the budget", because anything cheaper is a lower bound. */
    synth_bench_juno_set_heavy_patch();
    printf("\n-- full engine HEAVY synthetic (chorus+HPF+output), budget 64000 --\n");
    printf("  osc=saw+pulse+sub+noise res=max env+lfomod chorus=I+II\n");
    bench_juno_sweep_voices();

    /* Patch 2: A15 Moving Strings, the numerical A/B preset. Low resonance so
     * the filter stays linear, which is what makes roundoff differences
     * measurable instead of being amplified by the feedback path. */
    if (synth_bench_juno_set_patch(SYNTH_BENCH_JUNO_PATCH_NUMERICAL) != 0) {
        printf("\n-- full engine NUMERICAL A15 Moving Strings --\n");
        bench_juno_report_patch(SYNTH_BENCH_JUNO_PATCH_NUMERICAL);
        bench_juno_sweep_voices();
    } else {
        printf("\n  numerical preset unavailable, skipped\n");
    }

    /* Patch 3: A57 Thud Wah, the listening preset. Resonance 0.701 sits just
     * under the self-oscillation knee, so it rings hard enough to hear a
     * timbre change without the patch degenerating into a test tone. */
    if (synth_bench_juno_set_patch(SYNTH_BENCH_JUNO_PATCH_LISTENING) != 0) {
        printf("\n-- full engine LISTENING A57 Thud Wah --\n");
        bench_juno_report_patch(SYNTH_BENCH_JUNO_PATCH_LISTENING);
        bench_juno_sweep_voices();
    } else {
        printf("\n  listening preset unavailable, skipped\n");
    }

    /* Patch 4: B76 Martian Chord, the self-oscillation preset. vcf_res 0.992 puts
     * k=4.83, past the self-oscillation knee, with sustain 0.992 so the filter
     * rings indefinitely. Every oscillator is off by design: the only signal is
     * the filter's own feedback, which isolates the VCF path. Expect this to be
     * the cheapest of the four in CPU terms and the most expensive to get
     * audibly wrong. */
    if (synth_bench_juno_set_patch(SYNTH_BENCH_JUNO_PATCH_SELFOSC) != 0) {
        printf("\n-- full engine SELF-OSC B76 Martian Chord --\n");
        bench_juno_report_patch(SYNTH_BENCH_JUNO_PATCH_SELFOSC);
        bench_juno_sweep_voices();
    } else {
        printf("\n  self-osc preset unavailable, skipped\n");
    }

    bench_cdc_diag("post-sweep");
    printf("[FULL-ENGINE SWEEP COMPLETE]\n");
}

/* Standalone voice-path sweep on a named factory patch, timing
 * voice_process_sample() directly so chorus / HPF / output are excluded.
 * This is the number to compare against the full-engine sweep to isolate the
 * per-voice cost from the fixed engine overhead.
 *
 * Off by default for the same reason bench_juno_voices() is: the full-engine
 * sweep already measures the same voices with the real output path, and the
 * boot log has a ~2 KB budget. Enable MCUNO_BENCH_VOICE_PATH to get both.
 */
__attribute__((unused)) static void bench_juno_voice_patch(SynthBenchJunoPatch which)
{
    JunoEngineParams p;

    if (synth_bench_juno_get_patch(which, &p) == 0) {
        printf("\n  patch %d unavailable, skipped\n", (int)which);
        return;
    }

    static JunoVoiceCard voice_cards[JUNO_NUM_VOICES];
    static VoiceTolerance voice_tol;

    for (int v = 0; v < JUNO_NUM_VOICES; v++) {
        voice_init(&voice_cards[v], v, &voice_tol, BENCH_JUNO_SR);
    }

    printf("\n-- voice path %s, %d-sample block, budget %lu --\n",
           synth_bench_juno_patch_name(which),
           BENCH_JUNO_BLOCK, (unsigned long)BENCH_JUNO_BLOCK_TICKS);

    float sink = 0.0f;
    unsigned int prev = 0;
    for (int n = 1; n <= JUNO_NUM_VOICES; n++) {
        for (int v = 0; v < n; v++) {
            voice_note_on(&voice_cards[v], 48 + v, 0.9f, 0.0f);
        }

        uint32_t total = 0;
        for (int sub = 0; sub < BENCH_JUNO_BLOCK / BENCH_JUNO_SUB; sub++) {
            uint32_t t0 = bench_ticks();
            for (int s = 0; s < BENCH_JUNO_SUB; s++) {
                for (int v = 0; v < n; v++) {
                    sink += voice_process_sample(&voice_cards[v], &p, 0.0f, 1.0f, 0.5f);
                }
            }
            total += (bench_ticks() - t0) & 0xFFFFu;
            wdt_clear(); /* outside the timed region */
        }

        unsigned int cur = (unsigned int)total;
        printf("  voices=%d  %6u ticks  %3u%%  (+%u/voice)\n",
               n, cur, (cur * 100u) / BENCH_JUNO_BLOCK_TICKS,
               (n > 1) ? (cur - prev) : cur);
        prev = cur;
    }

    if (sink == 12345.6789f) {
        printf("  ");
    }
}

/* -------------------------------------------------------------------------
 * CDC TX health probe.
 *
 * putchar() -> mcu_cdc_putc() -> cdc_ring_push(), which silently discards
 * into s_cdc_dropped_bytes when the 2048-byte ring is full (mcu_usb.c:712).
 * The ring only advances when the USB ISR clears s_ep1_in_busy and re-pumps
 * (mcu_usb.c:558). That is why the report always dies around ~2048 bytes while
 * pre-scheduler, and why post-scheduler ADC/pad logging runs forever.
 *
 * Reading the counter distinguishes "our ring overflowed" from "the bytes left
 * the ring but were lost downstream", which the visible log alone cannot tell.
 * ------------------------------------------------------------------------- */
static void bench_cdc_diag(const char *tag)
{
    printf("  [CDC %-8s] dtr=%u dropped_total=%u\n",
           tag, (unsigned int)(mcu_cdc_is_connected() ? 1u : 0u),
           mcu_cdc_get_dropped_bytes());
}

void bench_run(void)
{
    bench_cdc_diag("pre");

    /* Distinguishes the three candidate explanations for a repeated report:
     *   #1 twice in one boot  -> main() was re-entered, no reset happened
     *   #1 again              -> this is a real MCU reset (bss cleared, and
     *                            the self-test would have re-run)
     *   (a partial repeat with no new number is a UART/DMA replay artifact) */
    static uint32_t run_seq;
    printf("\n=== FP BENCH run#%lu ===\n", (unsigned long)(run_seq + 1u));

    bench_stage("S0 timer");
    bench_timer_init();
    bench_timer_probe();

    int fma_ok = bench_fma_correct();
    if (!fma_ok) {
        printf("FMA ** WRONG ** -- encoding suspect\n");
    }
    bench_stage("S1 fma done");
    bench_fp_cost_probe();
    bench_voice_components();
    printf("\n");

    /* RESULTS FIRST. Everything decision-relevant is emitted ahead of the
     * verbose reference tables, so that if the stream is cut short by the
     * capture the numbers that actually matter are the ones still visible. */
    /* Standalone voice-path sweep. Superseded by the full-engine sweep, which
     * measures the same voices with the real chorus/HPF/output path included, so
     * it is off by default to leave boot-log budget for that. */
#ifndef MCUNO_BENCH_VOICE_PATH
#define MCUNO_BENCH_VOICE_PATH 0
#endif
#if MCUNO_BENCH_VOICE_PATH
    bench_juno_voices();
    bench_stage("S2 juno done");
#endif

    /* VCF reciprocal error characterisation. Already characterised and stable
     * across five builds (max abs 2.98e-08 on t, 2.38e-07 on df), so it is off by
     * default rather than spending boot-log budget on a re-measurement. */
#ifndef MCUNO_BENCH_VCF
#define MCUNO_BENCH_VCF 0
#endif
#if MCUNO_BENCH_VCF
    bench_vcf_recip_error();
    bench_stage("S3 vcf done");
#endif

    printf("\n[RESULTS COMPLETE]\n");

    /* The reference tables are ~700 bytes of already-known constant data, and
     * the whole report has to stay under the ~2 KB printf output budget
     * measured from two independent builds or the tail is silently dropped.
     * They move behind a flag so the numbers still being collected survive. */
#ifndef MCUNO_BENCH_REFERENCE_TABLES
#define MCUNO_BENCH_REFERENCE_TABLES 0
#endif
#if MCUNO_BENCH_REFERENCE_TABLES
    bench_primitives();
    bench_stage("S6 prim done");
    printf("\n");
    bench_tanh();
    bench_stage("S7 tanh done");
#endif

    bench_cdc_diag("post");
    printf("\nBENCH END ok ticks=%lu\n", (unsigned long)bench_ticks());
    wdt_clear();

    /* Incremented only once the flush is known to have completed, so a repeated
     * report still prints the same number if it dies mid-flush. */
    run_seq++;
}