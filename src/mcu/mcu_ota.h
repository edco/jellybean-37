/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef MCU_OTA_H_
#define MCU_OTA_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Early boot cleanup: erases leftover staged update loader at 0x000E0000
 *        if present, so that warm resets or crashes do not re-enter update mode.
 */
int mcu_ota_boot_cleanup(void);

/**
 * @brief Called from mcu_midi SysEx parser upon receiving F0 22 24 35 7F F7.
 */
void mcu_ota_request_session(void);

/**
 * @brief Called from mcu_midi SysEx parser when status query 0x11 is received.
 */
void mcu_ota_reply_identity(void);

/**
 * @brief Called from mcu_midi when an incoming 7-bit SysEx payload (between F0 and F7) is received.
 */
void mcu_ota_on_sysex_frame(const uint8_t *frame_payload, size_t len);

/**
 * @brief Polled from thread context (GUI task loop) to check and run pending OTA session.
 */
void mcu_ota_check_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* MCU_OTA_H_ */
