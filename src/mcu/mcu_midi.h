/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_midi.h
 * USB MIDI 1.0 Packet Engine, SysEx Parser, and TLC Protocol Interpreter
 */

#ifndef MCU_MIDI_H
#define MCU_MIDI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize USB MIDI parser and transmit buffers.
 */
void mcu_midi_init(void);

/**
 * Process a block of incoming USB MIDI packets from EP4 OUT.
 * @param buffer Raw buffer containing 4-byte CIN packets
 * @param len Number of bytes received (multiple of 4)
 */
void mcu_midi_process_rx_packet(const uint8_t *buffer, size_t len);

/**
 * Queue a 4-byte USB-MIDI packet for transmission on EP4 IN.
 * @return true if queued successfully, false if queue full.
 */
bool mcu_midi_queue_tx_packet(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3);

/**
 * Flush queued MIDI packets to USB EP4 IN DMA if endpoint is idle.
 * Called from USB interrupt or main poll loop.
 */
void mcu_midi_poll_tx(void);

/**
 * Callback invoked when EP4 IN DMA transmission completes.
 */
void mcu_midi_on_tx_complete(void);

#ifdef __cplusplus
}
#endif

#endif /* MCU_MIDI_H */
