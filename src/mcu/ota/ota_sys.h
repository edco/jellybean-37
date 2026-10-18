/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef OTA_SYS_H
#define OTA_SYS_H

#include <stdint.h>
#include <stddef.h>

#include "asm/WL82.h"
#include "asm/usb.h"

/* SIE Indirect Register Access Helpers */
static inline void sie_write(uint32_t reg, uint32_t val)
{
    if (!(JL_USB->CON0 & (1u << USB_NRST))) {
        return;
    }
    JL_USB->CON1 = ((reg << 8) & 0xFFFF00u) | (val & 0xFFu);
    __asm__ volatile("csync" ::: "memory");
    uint32_t timeout = 20000u;
    while (!(JL_USB->CON1 & (1u << MACK)) && --timeout) {
    }
}

static inline uint32_t sie_read(uint32_t reg)
{
    if (!(JL_USB->CON0 & (1u << USB_NRST))) {
        return 0;
    }
    JL_USB->CON1 = ((reg << 8) & 0xFFBF00u) | (1u << MC_RNW);
    __asm__ volatile("csync" ::: "memory");
    uint32_t timeout = 20000u;
    while (!(JL_USB->CON1 & (1u << MACK)) && --timeout) {
    }
    return JL_USB->CON1 & 0xFFu;
}

/* System Soft Reset */
static inline void sys_soft_reset(void)
{
    JL_POWER->CON |= (1u << 4);
    for (;;) {
        __asm__ volatile("nop");
    }
}

/* Memory Sync Barrier */
static inline void sys_sync(void)
{
    __asm__ volatile("csync" ::: "memory");
}

/* P33 PMU/RTC Hardware Register Access */
static inline uint8_t p33_buf_raw(uint8_t val)
{
    JL_P33->SPI_DAT = val;
    JL_P33->SPI_CON |= 0x10u;
    sys_sync();
    while (JL_P33->SPI_CON & 0x02u) {
    }
    return (uint8_t)JL_P33->SPI_DAT;
}

static inline void p33_tx_1byte_raw(uint16_t addr, uint8_t data)
{
    JL_P33->SPI_CON &= ~0x0100u;
    JL_P33->SPI_CON |= 0x0001u;
    sys_sync();
    p33_buf_raw((uint8_t)((addr >> 8) & 0x03));
    p33_buf_raw((uint8_t)(addr & 0xFF));
    p33_buf_raw(data);
    JL_P33->SPI_CON &= ~0x0101u;
    sys_sync();
}

/* Hardware Watchdog Service */
static inline void wdt_close(void)
{
    /* P3_WDT_CON (0x80) = 0: Disable hardware watchdog */
    p33_tx_1byte_raw(0x80, 0);
}

static inline void wdt_clear(void)
{
    /* P3_WDT_CON (0x80) |= 0x40: Clear / kick hardware watchdog */
    p33_tx_1byte_raw(0x80, 0x40);
}

/* Memory Functions for Freestanding Environment */
void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    while (n--) {
        if (*p1 != *p2) {
            return (int)*p1 - (int)*p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

#endif /* OTA_SYS_H */
