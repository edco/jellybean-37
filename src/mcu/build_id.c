/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file build_id.c
 * Firmware Build-ID runtime accessor and diagnostic formatting implementation.
 */

#include "build_id.h"
#include <string.h>

/* Weak symbols so code links cleanly even if linker script doesn't define them */
__attribute__((weak)) extern const uint8_t __start_gnu_build_id[];
__attribute__((weak)) extern const uint8_t __stop_gnu_build_id[];

/* 3-bit color code mapping matching MCU visual diagnostic color palette:
 * 0 (000): Black (K)
 * 1 (001): Blue (B)
 * 2 (010): Green (G)
 * 3 (011): Cyan (C)
 * 4 (100): Red (R)
 * 5 (101): Magenta (M)
 * 6 (110): Yellow (Y)
 * 7 (111): White (W)
 */
static const char COLOR_NAMES_3BIT[8] = {
    'K', 'B', 'G', 'C', 'R', 'M', 'Y', 'W'
};

const ElfGnuBuildIdNote *build_id_get_note(void)
{
    uintptr_t start = (uintptr_t)__start_gnu_build_id;
    uintptr_t stop  = (uintptr_t)__stop_gnu_build_id;

    if (start == 0 || stop == 0 || start >= stop) {
        return NULL;
    }

    const ElfGnuBuildIdNote *note =
        (const ElfGnuBuildIdNote *)(const void *)__start_gnu_build_id;

    /* Validate ELF Note header for NT_GNU_BUILD_ID */
    if (note->namesz == 4 &&
        note->type == 3 &&
        note->name[0] == 'G' &&
        note->name[1] == 'N' &&
        note->name[2] == 'U' &&
        note->name[3] == '\0') {
        return note;
    }

    return NULL;
}

const uint8_t *build_id_get_raw(size_t *out_len)
{
    const ElfGnuBuildIdNote *note = build_id_get_note();
    if (note != NULL) {
        if (out_len != NULL) {
            *out_len = note->descsz;
        }
        return note->desc;
    }

    if (out_len != NULL) {
        *out_len = 0;
    }
    return NULL;
}

uint32_t build_id_get_24bit(void)
{
    size_t len = 0;
    const uint8_t *raw = build_id_get_raw(&len);
    if (raw != NULL && len >= 3) {
        return ((uint32_t)raw[0] << 16) |
               ((uint32_t)raw[1] << 8)  |
               ((uint32_t)raw[2]);
    }
    return 0;
}

size_t build_id_format_hex(char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size == 0) {
        return 0;
    }

    size_t len = 0;
    const uint8_t *raw = build_id_get_raw(&len);
    if (raw == NULL || len == 0) {
        buf[0] = '\0';
        return 0;
    }

    static const char hex_chars[] = "0123456789abcdef";
    size_t out_idx = 0;
    for (size_t i = 0; i < len && (out_idx + 2) < buf_size; i++) {
        buf[out_idx++] = hex_chars[(raw[i] >> 4) & 0x0F];
        buf[out_idx++] = hex_chars[raw[i] & 0x0F];
    }
    buf[out_idx] = '\0';
    return out_idx;
}

size_t build_id_format_color_codes(char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size < 9) {
        if (buf != NULL && buf_size > 0) {
            buf[0] = '\0';
        }
        return 0;
    }

    uint32_t stamp24 = build_id_get_24bit();
    for (int i = 0; i < 8; i++) {
        uint8_t shift = (7 - i) * 3;
        uint8_t chunk = (stamp24 >> shift) & 0x7;
        buf[i] = COLOR_NAMES_3BIT[chunk];
    }
    buf[8] = '\0';
    return 8;
}

size_t build_id_format_color_codes_spaced(char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size < 16) {
        if (buf != NULL && buf_size > 0) {
            buf[0] = '\0';
        }
        return 0;
    }

    uint32_t stamp24 = build_id_get_24bit();
    size_t out_idx = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t shift = (7 - i) * 3;
        uint8_t chunk = (stamp24 >> shift) & 0x7;
        buf[out_idx++] = COLOR_NAMES_3BIT[chunk];
        if (i < 7) {
            buf[out_idx++] = ' ';
        }
    }
    buf[out_idx] = '\0';
    return out_idx;
}
