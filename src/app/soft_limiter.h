/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file soft_limiter.h
 * Padé rational soft-knee audio limiter with early-exit and branchless options.
 */

#ifndef SOFT_LIMITER_H
#define SOFT_LIMITER_H

#include <math.h>
#include <stdbool.h>

#define SOFT_LIMITER_DEFAULT_THRESHOLD 0.95f
#define SOFT_LIMITER_DEFAULT_MAX       4.0f

typedef struct {
    float T;
    float M;
    float k0; /* (M - T) * u * (u - 1) */
    float k1; /* u * (u - 2) where u = (M - T) / (1 - T) */
    bool  disabled;
} SoftLimiterPade;

/**
 * Initialize soft limiter coefficients.
 * @param lim SoftLimiterPade instance
 * @param T Threshold (0.00 to 1.00; 1.00 = disabled)
 * @param M Maximum ceiling (1.0 to 8.0; 1.0 = disabled)
 */
static inline void soft_limiter_init(SoftLimiterPade *lim, float T, float M)
{
    if (T < 0.0f) T = 0.0f;
    if (T > 1.0f) T = 1.0f;
    if (M < 1.0f) M = 1.0f;
    if (M > 8.0f) M = 8.0f;

    lim->T = T;
    lim->M = M;

    /* Disabled when T >= 1.0 (threshold at 0dBFS) or M <= 1.0 or T >= M */
    if (T >= 1.0f || M <= 1.0f || T >= M) {
        lim->disabled = true;
        lim->k0 = 0.0f;
        lim->k1 = 0.0f;
    } else {
        lim->disabled = false;
        float D = M - T;
        float u = D / (1.0f - T);
        lim->k1 = u * (u - 2.0f);
        lim->k0 = D * u * (u - 1.0f);
    }
}

/**
 * 1. Early-exit version: Fastest when signal is mostly below T (skips division).
 */
static inline float soft_limit_branchy(const SoftLimiterPade *lim, float x)
{
    float a = fabsf(x);

    if (lim->disabled) {
        if (a > 1.0f) return copysignf(1.0f, x);
        return x;
    }

    if (a <= lim->T) {
        return x;
    }
    if (a >= lim->M) {
        return copysignf(1.0f, x);
    }

    float d = lim->M - a;
    float denom = lim->k0 - lim->k1 * d;
    if (denom <= 0.0f) {
        return copysignf(1.0f, x);
    }

    float y = 1.0f - (d * d) / denom;
    return copysignf(y, x);
}

/**
 * 2. Branchless / SIMD-friendly version: Constant-time execution.
 */
static inline float soft_limit_branchless(const SoftLimiterPade *lim, float x)
{
    float a = fabsf(x);

    if (lim->disabled) {
        float y_clip = fminf(a, 1.0f);
        return copysignf(y_clip, x);
    }

    float a_clamped = fminf(fmaxf(a, lim->T), lim->M);
    float d = lim->M - a_clamped;
    float denom = lim->k0 - lim->k1 * d;
    float y_knee = (denom > 0.0f) ? (1.0f - (d * d) / denom) : 1.0f;

    float y = (a <= lim->T) ? a : y_knee;
    return copysignf(y, x);
}

#endif /* SOFT_LIMITER_H */
