/**
 * @file dexed_compat.h
 * Compatibility definitions for compiling Synth_Dexed outside Arduino/Circle environments.
 */

#ifndef DEXED_COMPAT_H
#define DEXED_COMPAT_H

#include <stdint.h>
#include <math.h>

#ifndef LG_N
#define LG_N 5
#endif

#ifndef _N_
#define _N_ (1 << LG_N)
#endif

#if LG_N != 5
#error "Dexed LG_N must be 5 (32 sample blocks)"
#endif

#if _N_ != 32
#error "Dexed _N_ must be 32 (32 sample blocks)"
#endif

#ifndef AUDIO_BLOCK_SAMPLES
#define AUDIO_BLOCK_SAMPLES 128
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

#ifndef F
#define F(s) (s)
#endif

#ifndef DEC
#define DEC 10
#endif

#ifdef __cplusplus
typedef float float32_t;

#include "hal_time.h"

static inline uint32_t millis(void)
{
    return hal_get_ms();
}

/* Zero-cost dummy Serial interface to satisfy Arduino debug logging */
struct DummySerial {
    template<typename... Args>
    void printf(const char *, Args &&...) {}
    template<typename T>
    void print(const T &) {}
    template<typename T>
    void println(const T &) {}
    template<typename T, typename U>
    void println(const T &, const U &) {}
};

static DummySerial Serial __attribute__((unused));

#if defined(TARGET_MCU)
#include "asm/cpu.h"
#endif

#endif /* __cplusplus */

#endif /* DEXED_COMPAT_H */
