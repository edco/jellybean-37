/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef OTA_COMMON_H
#define OTA_COMMON_H

#include <stdint.h>
#include <stddef.h>

/* SPI NOR Flash Parameters & Commands */
#define FLASH_SECTOR_SIZE           4096u
#define FLASH_PAGE_SIZE             256u

#define FLASH_CMD_WREN              0x06u
#define FLASH_CMD_RDSR              0x05u
#define FLASH_CMD_SECTOR_ERASE      0x20u
#define FLASH_CMD_PAGE_PROGRAM      0x02u
#define FLASH_CMD_FAST_READ         0x0Bu

/* Flash Partition & Memory Layout */
#define FLASH_APP_START             0x00004000u
#define FLASH_APP_END               0x00093000u
#define FLASH_APP_END_STOCK         0x0009C000u
#define FLASH_APP_START_ADDR        FLASH_APP_START
#define FLASH_APP_END_ADDR          FLASH_APP_END_STOCK
#define FLASH_APP_SIZE              (FLASH_APP_END - FLASH_APP_START)
#define FLASH_APP_SIZE_STOCK        (FLASH_APP_END_STOCK - FLASH_APP_START)
#define FLASH_TOTAL_SIZE            0x00100000u
#define UFW_FLASH_OFFSET            0x00000400u
#define OTA_CHUNK_SIZE              512u

#define OTA_STAGING_ADDR            0x000E0000u
#define OTA_STAGING_LEN             0x00005000u
#define OTA_PARM_FLASH_ADDR         (OTA_STAGING_ADDR + OTA_STAGING_LEN - 0x100u) /* 0x000E4F00 */
#define OTA_PARM_RAM_ADDR           ((volatile uint8_t *)0x01C7FD88u)
#define OTA_PARM_LEN                112u
#define OTA_FRAME_MAX               640u

/* M-VAVE TLC Update Protocol Constants */
#define TLC_MAGIC_0                 0x00u
#define TLC_MAGIC_1                 0x59u
#define TLC_CMD_STATUS_QUERY        0x11u
#define TLC_CMD_MODE_QUERY          0x12u
#define TLC_CMD_CHUNK_REQUEST       0x30u

#define TLC_ADDR_FINISH             0xF0000000u
#define TLC_ADDR_STAGING_READY      0xE0000000u

/* Compatibility aliases for TLC protocol */
#define TLC_TYPE_STATUS_QUERY       TLC_CMD_STATUS_QUERY
#define TLC_TYPE_MODE_QUERY         TLC_CMD_MODE_QUERY
#define TLC_TYPE_CHUNK_WRITE        TLC_CMD_CHUNK_REQUEST
#define TLC_STATE_RUNNING           TLC_ADDR_STAGING_READY
#define TLC_STATE_DONE              TLC_ADDR_FINISH

#define IDENTITY_STRING_STAGE1      "SMK-37 Pro_001"
#define IDENTITY_STRING_STAGE2      "ota-SMK-37 Pro_001"

/* Little-endian integer serialize / deserialize helpers */
static inline uint32_t ota_rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static inline uint32_t ota_rd24(const uint8_t *p)
{
    return ota_rd16(p) | ((uint32_t)p[2] << 16);
}

static inline uint32_t ota_rd32(const uint8_t *p)
{
    return ota_rd24(p) | ((uint32_t)p[3] << 24);
}

static inline void ota_wr16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void ota_wr32(uint8_t *p, uint32_t v)
{
    ota_wr16(p, v);
    ota_wr16(p + 2, v >> 16);
}

/* CRC-16 (CCITT, polynomial 0x1021) */
static inline uint16_t ota_calc_crc16(const uint8_t *data, uint32_t len, uint16_t init_crc)
{
    uint16_t crc = init_crc;
    while (len--) {
        crc ^= (uint16_t)*data++ << 8;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x8000u) ? ((crc << 1) ^ 0x1021u) : (crc << 1);
        }
    }
    return crc;
}

/* Pack 8-bit octets into MIDI 7-bit words (Pack7) */
static inline uint32_t ota_encode_pack7(const uint8_t *in, uint32_t in_len, uint8_t *out)
{
    uint32_t acc = 0, num_bits = 0, out_len = 0;
    while (in_len--) {
        acc |= (uint32_t)*in++ << num_bits;
        num_bits += 8u;
        while (num_bits >= 7u) {
            out[out_len++] = (uint8_t)(acc & 0x7Fu);
            acc >>= 7;
            num_bits -= 7u;
        }
    }
    if (num_bits > 0) {
        out[out_len++] = (uint8_t)(acc & 0x7Fu);
    }
    return out_len;
}

/* Unpack MIDI 7-bit words into 8-bit octets (Unpack7) */
static inline uint32_t ota_decode_unpack7(const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t max_out)
{
    uint32_t acc = 0, num_bits = 0, out_len = 0;
    while (in_len--) {
        acc |= (uint32_t)(*in++ & 0x7Fu) << num_bits;
        num_bits += 7u;
        if (num_bits >= 8u) {
            if (out_len < max_out) {
                out[out_len] = (uint8_t)acc;
            }
            out_len++;
            acc >>= 8;
            num_bits -= 8u;
        }
    }
    return out_len;
}

/* JieLi UFW stream cipher scramble / descramble (LFSR polynomial 0x1021) */
static inline void ota_scramble_payload(uint8_t *data, uint32_t len)
{
    uint32_t key = 0xFFFFu;
    while (len--) {
        *data++ ^= (uint8_t)key;
        key = ((key << 1) ^ ((key & 0x8000u) ? 0x1021u : 0u)) & 0xFFFFu;
    }
}

#endif /* OTA_COMMON_H */
