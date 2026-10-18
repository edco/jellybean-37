/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_time.c
 * MCU Timing implementation wrapping vendor hardware Timer1, Timer0, and FreeRTOS
 */

#include "hal_time.h"
#include <stdint.h>

extern int timer1_init(void);
extern uint32_t timer_get_ms(void);
extern void hw_udelay(uint32_t usec);
extern uint32_t local_timer_us_time(void);

extern int sys_timer_init(void);

extern uint32_t jiffies_msec(void);

void hal_time_init(void)
{
    /* Initialize vendor software timer subsystem (initializes usr_timer_head, sys_timer_sem, and timer1) */
    sys_timer_init();
}

uint32_t hal_get_ms(void)
{
    uint32_t ms = timer_get_ms();
    if (ms != 0) {
        return ms;
    }
    return jiffies_msec();
}

void hal_delay_ms(uint32_t ms)
{
    while (ms--) {
        hw_udelay(1000);
    }
}

void hal_delay_us(uint32_t us)
{
    hw_udelay(us);
}
