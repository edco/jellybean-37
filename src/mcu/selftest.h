/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file selftest.h
 * Self-test interface for verified replacement of JieLi SDK drivers.
 */

#ifndef SELFTEST_H
#define SELFTEST_H

/**
 * Run comprehensive on-device self-test verifying:
 * 1. Memory allocation (malloc/free)
 * 2. String formatting (snprintf)
 * 3. P33 power bus control (P33_CON_SET / p33_rx_1byte)
 * 4. Watchdog reset service (wdt_clear)
 * 5. Hardware timer precision delays (hw_udelay / timer_get_ms)
 * 6. SAR-ADC analog conversions (adc_get_value / adc_get_voltage)
 *
 * Displays results visually on ST7789 Row 5 (Green=Pass, Red=Fail).
 * Returns 1 if all tests pass, 0 on failure.
 */
#if defined(STARTUP_DIAGNOSTICS) && STARTUP_DIAGNOSTICS
int selftest_run(void);
#else
static inline int selftest_run(void) { return 1; }
#endif

#endif /* SELFTEST_H */
