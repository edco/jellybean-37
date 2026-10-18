/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file bench.h
 * @brief On-target microbenchmarks for pi32v2 scalar FP behaviour.
 *
 * Answers three questions that cannot be settled from the host:
 *   1. Does the pi32v2 fused multiply-add actually execute correctly, and does
 *      it round once (true FMA) or twice (mul-then-add in a fused slot)?
 *      The JieLi LLVM backend has no FMUL-ADD opcode, so the toolchain never
 *      emits it and nothing in the SDK proves the FPU implements it.
 *   2. Is it actually faster than a separate FMUL + FADD?
 *   3. What does a polynomial tanh cost versus libm tanhf, and what is the
 *      error actually incurred?
 *
 * Selected at the assembler level: pi32v2 has no `fmadd` mnemonic. The FPU
 * instruction set is Blackfin-derived and uses algebraic syntax, so the
 * fused op is written as an equation with a float suffix, e.g.
 *
 *     __asm__ ("%0 += %1 * %2 (f)" : "+r"(a) : "r"(b), "r"(c));
 *
 * Confirmed to assemble to the fused encoding (opcode 7) rather than being
 * split into a multiply and an add.
 *
 * Timebase: JL_TIMER4 counting the 24 MHz oscillator as a 32-bit free-running
 * counter (PRD = 0xFFFFFFFF, 1 tick = 41.667 ns, wraps every 178.9 s).
 * See bench_timer_init() for why this timer has to be started here.
 */

#ifndef BENCH_H
#define BENCH_H

#if defined(STARTUP_DIAGNOSTICS) && STARTUP_DIAGNOSTICS
/** Run all benchmarks and print a report over the debug UART. */
void bench_run(void);

/**
 * Time the real juno_engine_process_block() at 1..6 voices.
 *
 * Must be called AFTER synth_init() and BEFORE vTaskStartScheduler(), so no task
 * can perturb a timed region. It cannot live inside bench_run(): that runs
 * before the synth engines are allocated (measured synth_init=0, s_juno=NULL at
 * that point), so it would only ever report a null engine.
 *
 * Separated from bench_run() also because the whole boot log shares a ~2 KB
 * printf output budget, and this section was being emitted into a report that
 * could not yet be valid.
 */
void bench_juno_full_sweep(void);
#else
static inline void bench_run(void) {}
static inline void bench_juno_full_sweep(void) {}
#endif

#endif /* BENCH_H */