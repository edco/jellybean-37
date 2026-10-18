/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_cdc.h
 * Non-blocking USB CDC-ACM Virtual COM Port / Serial Logging Driver
 */

#ifndef MCU_CDC_H
#define MCU_CDC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
/**
 * Output a single character non-blockingly to the CDC TX ring buffer.
 * If buffer is full or DTR is not asserted, character is dropped immediately.
 */
void mcu_cdc_putc(char c);

/**
 * Output a buffer non-blockingly to the CDC TX ring buffer.
 * Bytes that exceed available space are dropped immediately.
 */
void mcu_cdc_write(const void *buf, size_t len);

/**
 * Output a null-terminated string non-blockingly.
 */
void mcu_cdc_puts(const char *str);

/**
 * Read a single received byte non-blockingly.
 * @return Byte received (0-255), or -1 if RX queue is empty.
 */
int mcu_cdc_getc(void);

/**
 * Read received bytes into buffer.
 * @return Number of bytes read.
 */
int mcu_cdc_read(void *buf, size_t max_len);

/**
 * Check if a host terminal is connected (DTR asserted).
 */
bool mcu_cdc_is_connected(void);

/**
 * Get total number of dropped bytes due to buffer overflow.
 */
uint32_t mcu_cdc_get_dropped_bytes(void);

/**
 * Number of bytes currently queued in the CDC TX ring, i.e. emitted by the
 * caller but not yet handed to the USB endpoint.
 *
 * This is the measurement that separates "the ring is backing up" from "the
 * ring drained fine and the loss happened elsewhere": the drop counter stays
 * at zero while the ring is merely filling, and only increments once it is
 * genuinely full.
 */
uint32_t mcu_cdc_get_tx_pending(void);

/**
 * Register an optional callback for incoming RX characters.
 */
void mcu_cdc_set_rx_callback(void (*cb)(uint8_t byte));

/**
 * Service the CDC TX DMA pump.
 */
void mcu_cdc_pump_tx(void);
#else
static inline void mcu_cdc_putc(char c) { (void)c; }
static inline void mcu_cdc_write(const void *buf, size_t len) { (void)buf; (void)len; }
static inline void mcu_cdc_puts(const char *str) { (void)str; }
static inline int mcu_cdc_getc(void) { return -1; }
static inline int mcu_cdc_read(void *buf, size_t max_len) { (void)buf; (void)max_len; return 0; }
static inline bool mcu_cdc_is_connected(void) { return false; }
static inline uint32_t mcu_cdc_get_dropped_bytes(void) { return 0; }
static inline uint32_t mcu_cdc_get_tx_pending(void) { return 0; }
static inline void mcu_cdc_set_rx_callback(void (*cb)(uint8_t byte)) { (void)cb; }
static inline void mcu_cdc_pump_tx(void) {}
#endif

#ifdef __cplusplus
}
#endif

#endif /* MCU_CDC_H */
