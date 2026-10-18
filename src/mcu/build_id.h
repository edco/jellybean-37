/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file build_id.h
 * Firmware Build-ID runtime accessor and diagnostic formatting.
 *
 * Exposes the linker-generated .note.gnu.build-id ELF section to firmware code
 * without requiring pre-build header generation.
 */

#ifndef BUILD_ID_H
#define BUILD_ID_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Standard ELF Note header for GNU Build-ID (32-bit).
 */
typedef struct {
    uint32_t namesz;    /**< Length of note name (e.g. 4 for "GNU\0") */
    uint32_t descsz;    /**< Length of descriptor / hash (e.g. 20 for SHA-1) */
    uint32_t type;      /**< Note type: 3 for NT_GNU_BUILD_ID */
    char     name[4];   /**< "GNU\0" */
    uint8_t  desc[];    /**< Raw build ID hash bytes */
} ElfGnuBuildIdNote;

/* Boundary symbols defined in linker script (flash_app.ld / ram_app.ld) */
extern const uint8_t __start_gnu_build_id[];
extern const uint8_t __stop_gnu_build_id[];

/**
 * Retrieve the pointer to the ELF note header for GNU Build-ID.
 * @return Pointer to note header, or NULL if unavailable.
 */
const ElfGnuBuildIdNote *build_id_get_note(void);

/**
 * Retrieve pointer to the raw Build-ID hash bytes and its length.
 * @param[out] out_len Output pointer to receive length in bytes (can be NULL).
 * @return Pointer to raw hash bytes, or NULL if unavailable.
 */
const uint8_t *build_id_get_raw(size_t *out_len);

/**
 * Extract the first 24 bits of the Build-ID as an unsigned 24-bit integer.
 * Used for visual diagnostics display on ST7789 LCD row 0.
 * @return 24-bit build stamp value (bits 23..0), or 0 if unavailable.
 */
uint32_t build_id_get_24bit(void);

/**
 * Format the full Build-ID as a lowercase hex string.
 * @param buf Output character buffer.
 * @param buf_size Size of buffer in bytes (recommended >= 41 for SHA-1).
 * @return Number of characters written (excluding null terminator).
 */
size_t build_id_format_hex(char *buf, size_t buf_size);

/**
 * Format the first 24 bits of the Build-ID as an 8-character color code string
 * using 3-bit color mapping (K, B, G, C, R, M, Y, W).
 * @param buf Output character buffer.
 * @param buf_size Size of buffer in bytes (must be >= 9 for 8 chars + null).
 * @return Number of characters written (8), or 0 on error.
 */
size_t build_id_format_color_codes(char *buf, size_t buf_size);

/**
 * Format the first 24 bits of the Build-ID as space-separated color codes
 * (e.g., "W K M Y G Y B C").
 * @param buf Output character buffer.
 * @param buf_size Size of buffer in bytes (must be >= 16).
 * @return Number of characters written, or 0 on error.
 */
size_t build_id_format_color_codes_spaced(char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* BUILD_ID_H */
