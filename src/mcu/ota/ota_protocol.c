/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "ota_common.h"
#include "ota_sys.h"
#include "ota_time.h"
#include "ota_flash.h"

extern void ota_usb_queue_tx_packet(uint8_t cin, uint8_t m0, uint8_t m1, uint8_t m2);
extern void ota_usb_poll(void);
extern void ota_usb_detach(void);
extern void ota_usb_reset_rx_state(void);
extern void ota_usb_flush_tx(void);

#define IDENTITY_STRING         IDENTITY_STRING_STAGE2

static void ota_send_raw_midi(const uint8_t *stream, uint32_t total_len)
{
    uint32_t idx = 0;
    while (idx < total_len) {
        uint32_t rem = total_len - idx;
        if (rem >= 3) {
            uint8_t cin = (idx + 3 == total_len && stream[idx + 2] == 0xF7) ? 0x07 : 0x04;
            ota_usb_queue_tx_packet(cin, stream[idx], stream[idx + 1], stream[idx + 2]);
            idx += 3;
        } else if (rem == 2) {
            uint8_t cin = (stream[idx + 1] == 0xF7) ? 0x06 : 0x04;
            ota_usb_queue_tx_packet(cin, stream[idx], stream[idx + 1], 0);
            idx += 2;
        } else if (rem == 1) {
            uint8_t cin = (stream[idx] == 0xF7) ? 0x05 : 0x0F;
            ota_usb_queue_tx_packet(cin, stream[idx], 0, 0);
            idx += 1;
        }
    }
}

static void ota_send_message(uint8_t cmd, const uint8_t *payload, uint32_t payload_len)
{
    uint8_t raw[8 + 64];
    uint8_t wire[2 + 128];
    uint32_t checksum = 0;

    raw[0] = 0x00;
    raw[1] = 0x59;
    raw[2] = cmd;
    raw[3] = (uint8_t)payload_len;
    raw[4] = (uint8_t)(payload_len >> 8);
    raw[5] = (uint8_t)(payload_len >> 16);

    for (uint32_t i = 0; i < payload_len; i++) {
        raw[6 + i] = payload[i];
        checksum += payload[i];
    }
    raw[6 + payload_len] = (uint8_t)~checksum;

    wire[0] = 0xF0;
    uint32_t enc_len = ota_encode_pack7(raw, 7 + payload_len, wire + 1);
    wire[1 + enc_len] = 0xF7;

    ota_send_raw_midi(wire, 2 + enc_len);
}

static void ota_send_identity(void)
{
    static const char id_str[] = IDENTITY_STRING;
    uint8_t id_buf[27];
    for (uint32_t i = 0; i < sizeof(id_buf); i++) {
        id_buf[i] = (i < sizeof(id_str) - 1) ? (uint8_t)id_str[i] : 0;
    }
    ota_send_message(0x11, id_buf, sizeof(id_buf));
}

/* Incoming SysEx Frame State */
static uint8_t rx_frame[768];
static uint32_t rx_frame_len = 0;
static volatile bool rx_frame_ready = false;

void ota_on_received_frame(const uint8_t *payload, uint32_t len)
{
    if (len > sizeof(rx_frame)) {
        return;
    }
    for (uint32_t i = 0; i < len; i++) {
        rx_frame[i] = payload[i];
    }
    rx_frame_len = len;
    rx_frame_ready = true;
}

bool ota_poll_handshake(void)
{
    static uint32_t identity_sent_ms = 0;
    static bool id_sent = false;

    if (identity_sent_ms != 0 && (time_get_ms() - identity_sent_ms) >= 1500) {
        ota_usb_flush_tx();
        ota_usb_reset_rx_state();
        rx_frame_ready = false;
        return true;
    }

    if (!rx_frame_ready) {
        return false;
    }

    /* Check for UPGRADE command: F0 22 24 35 7F F7 */
    if (rx_frame_len >= 4 && rx_frame[0] == 0x22 && rx_frame[1] == 0x24 && rx_frame[2] == 0x35) {
        rx_frame_ready = false;
        ota_usb_flush_tx();
        ota_usb_reset_rx_state();
        return true;
    }

    uint8_t unpacked[768];
    uint32_t dec_len = ota_decode_unpack7(rx_frame, rx_frame_len, unpacked, sizeof(unpacked));
    rx_frame_ready = false;

    if (dec_len >= 7 && unpacked[0] == 0x00 && unpacked[1] == 0x59) {
        /* Identity inquiry (0x11): respond with Stage 2 identity once */
        if (unpacked[2] == 0x11) {
            if (!id_sent) {
                ota_send_identity();
                id_sent = true;
                identity_sent_ms = time_get_ms();
            }
            return false;
        }
        /* If host directly requests chunk or start: commence flashing */
        if (unpacked[2] == 0x30) {
            ota_usb_flush_tx();
            ota_usb_reset_rx_state();
            return true;
        }
    }

    return false;
}

static int ota_poll_incoming_chunk(uint32_t expected_addr, uint8_t *dest, uint32_t expected_len, uint32_t timeout_ms)
{
    uint8_t unpacked[768];
    uint32_t start_ms = time_get_ms();
    uint32_t last_wdt_ms = start_ms;

    while ((time_get_ms() - start_ms) < timeout_ms) {
        if ((time_get_ms() - last_wdt_ms) >= 500) {
            wdt_clear();
            last_wdt_ms = time_get_ms();
        }
        ota_usb_poll();

        if (!rx_frame_ready) {
            continue;
        }

        uint32_t dec_len = ota_decode_unpack7(rx_frame, rx_frame_len, unpacked, sizeof(unpacked));
        rx_frame_ready = false;

        if (dec_len < 7 || unpacked[0] != 0x00 || unpacked[1] != 0x59) {
            continue;
        }

        /* Check for response to chunk read request 0x30 */
        if (unpacked[2] == 0x30 && dec_len >= (expected_len + 15)) {
            uint32_t chunk_addr = (uint32_t)unpacked[7] | ((uint32_t)unpacked[8] << 8) |
                                  ((uint32_t)unpacked[9] << 16) | ((uint32_t)unpacked[10] << 24);
            uint32_t chunk_len = (uint32_t)unpacked[11] | ((uint32_t)unpacked[12] << 8) |
                                 ((uint32_t)unpacked[13] << 16);

            if (chunk_addr == expected_addr && chunk_len == expected_len) {
                for (uint32_t i = 0; i < expected_len; i++) {
                    dest[i] = unpacked[14 + i];
                }
                return 0;
            }
        }
    }
    return -1;
}

static int ota_request_chunk(uint32_t addr, uint8_t *dest, uint32_t len)
{
    uint8_t req[8];
    req[0] = 0x00; /* Read request type */
    req[1] = (uint8_t)addr;
    req[2] = (uint8_t)(addr >> 8);
    req[3] = (uint8_t)(addr >> 16);
    req[4] = (uint8_t)(addr >> 24);
    req[5] = (uint8_t)len;
    req[6] = (uint8_t)(len >> 8);
    req[7] = 0x00;

    for (int retry = 0; retry < 12; retry++) {
        wdt_clear();
        ota_usb_reset_rx_state();
        rx_frame_ready = false;
        ota_send_message(0x30, req, sizeof(req));
        ota_usb_poll();
        if (ota_poll_incoming_chunk(addr, dest, len, 2500) == 0) {
            return 0;
        }
        time_delay_ms(20);
    }
    return -1;
}

void ota_cleanup_and_reboot(void)
{
    /* Clear flash staging partition (5 x 4KB sectors) */
    for (uint32_t off = 0; off < 0x5000; off += FLASH_SECTOR_SIZE) {
        flash_erase_sector(OTA_STAGING_ADDR + off);
    }

    /* Clear UPDATA_PARM in retention RAM */
    for (uint32_t i = 0; i < OTA_PARM_LEN; i++) {
        OTA_PARM_RAM_ADDR[i] = 0;
    }

    /* Detach USB to notify host cleanly */
    ota_usb_detach();

    /* Trigger soft reset into normal application boot */
    sys_soft_reset();
}

int ota_run_flashing_loop(void)
{
    static uint8_t sector_buffer[FLASH_SECTOR_SIZE];
    uint16_t running_crc = 0;

    /* Pull and flash application partition sector-by-sector from 0x00004000 to 0x00093000 */
    for (uint32_t sec_addr = FLASH_APP_START; sec_addr < FLASH_APP_END; sec_addr += FLASH_SECTOR_SIZE) {
        wdt_clear();

        /* 1. Pull all 8 chunks (4096 bytes) for this entire sector into RAM first */
        for (uint32_t chunk_off = 0; chunk_off < FLASH_SECTOR_SIZE; chunk_off += OTA_CHUNK_SIZE) {
            uint32_t cur_addr = sec_addr + chunk_off;
            uint32_t req_addr = UFW_FLASH_OFFSET + cur_addr;
            if (ota_request_chunk(req_addr, sector_buffer + chunk_off, OTA_CHUNK_SIZE) != 0) {
                /* Transfer failed or host disconnected: abort immediately WITHOUT touching flash */
                return -2;
            }
            running_crc = ota_calc_crc16(sector_buffer + chunk_off, OTA_CHUNK_SIZE, running_crc);

            time_delay_ms(1);
            ota_usb_poll();
        }

        /* 2. ONLY IF all 4096 bytes of the sector were successfully received in RAM,
         * erase the flash sector and program all 16 pages */
        if (flash_erase_sector(sec_addr) != 0) {
            return -1;
        }

        for (uint32_t page_off = 0; page_off < FLASH_SECTOR_SIZE; page_off += FLASH_PAGE_SIZE) {
            if (flash_program_chunk(sec_addr + page_off, sector_buffer + page_off, FLASH_PAGE_SIZE) != 0) {
                return -3;
            }
        }

        /* Allow USB SIE to settle and drain any stray state before next sector */
        time_delay_ms(2);
        ota_usb_poll();
        ota_usb_reset_rx_state();
        rx_frame_ready = false;
    }

    /* Notify host of successful completion (status address 0xF0000000) */
    uint8_t finish_req[8];
    finish_req[0] = 0x00;
    finish_req[1] = 0x00;
    finish_req[2] = 0x00;
    finish_req[3] = 0x00;
    finish_req[4] = 0xF0; /* Address 0xF0000000 */
    finish_req[5] = 0x08;
    finish_req[6] = 0x00;
    finish_req[7] = 0x00;

    for (int i = 0; i < 4; i++) {
        ota_send_message(0x30, finish_req, sizeof(finish_req));
        time_delay_ms(50);
        ota_usb_poll();
    }

    /* Flash update complete - cleanup and reboot */
    ota_cleanup_and_reboot();
    return 0;
}
