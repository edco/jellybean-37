/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_usb.h
 * USB Hardware Controller Driver for JieLi AC791N (WL82)
 */

#ifndef MCU_USB_H
#define MCU_USB_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize USB hardware peripheral, endpoints, descriptors, and IRQ 9.
 * @return 0 on success, negative error code on failure.
 */
int mcu_usb_init(void);

/**
 * Check if the USB device is attached and configured by the host.
 */
bool mcu_usb_is_configured(void);

/**
 * Check if Audio Streaming (host recording from synth) is currently active.
 */
bool mcu_usb_audio_is_active(void);

/**
 * Transmit a block of MIDI packets (up to 64 bytes) via Bulk EP4 IN DMA.
 * @param data Buffer containing 4-byte CIN packets
 * @param len Total length in bytes (must be <= 64)
 * @return true if transmission started, false if endpoint busy.
 */
bool mcu_usb_ep4_tx(const uint8_t *data, size_t len);

/**
 * Enqueue stereo PCM samples for transmission to host via Isochronous EP3 IN.
 * Called from real-time audio render superloop on Core 1 or DMA interrupt.
 * @param pcm16 Interleaved 16-bit signed stereo samples [L0, R0, L1, R1, ...]
 * @param frames Number of stereo frames (sample pairs)
 */
void mcu_usb_audio_push_samples(const int16_t *pcm16, size_t frames);

/**
 * Periodic poll function called from application loop.
 */
void mcu_usb_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* MCU_USB_H */
