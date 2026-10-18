/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_midi.c
 * USB MIDI 1.0 Packet Engine, SysEx Parser, and TLC Protocol Interpreter
 */

#include "mcu_midi.h"
#include "mcu_ota.h"
#include "ota/ota_common.h"
#include "hal_midi.h"
#include "hal_synth.h"
#include "spsc_ring.h"
#include "synth_engine.h"
#include <string.h>

/* Outgoing MIDI Packet Ring Buffer (FIFO) */
#define MIDI_TX_RING_CAPACITY   128     /* 128 packets * 4 bytes = 512 bytes */

typedef struct {
    uint8_t data[4];
} UsbMidiPkt;

static UsbMidiPkt tx_ring[MIDI_TX_RING_CAPACITY];
static volatile uint16_t tx_head = 0;
static volatile uint16_t tx_tail = 0;
static volatile bool tx_busy = false;

/* SysEx / TLC Stream Accumulator Buffer */
#define SYSEX_ACCUM_MAX         1024
static uint8_t sysex_buf[SYSEX_ACCUM_MAX];
static uint16_t sysex_len = 0;
static bool sysex_in_progress = false;

#if !defined(TARGET_RAM)
static bool ota_mode_active = false;
static bool ota_completed = false;
static uint32_t expected_chunk_offset = 0;
static uint32_t total_image_size = 0;
#endif

#ifdef TARGET_MCU
extern void p33_soft_reset(void);
#if !defined(TARGET_RAM)
extern int norflash_ioctl(void *device, uint32_t cmd, uint32_t arg);
extern int norflash_write(void *device, void *buf, uint32_t len, uint32_t offset);
#endif
#endif

/* External low-level USB transmission function provided by mcu_usb.c */
extern bool mcu_usb_ep4_tx(const uint8_t *data, size_t len);

void mcu_midi_init(void)
{
    tx_head = 0;
    tx_tail = 0;
    tx_busy = false;
    sysex_len = 0;
    sysex_in_progress = false;
#if !defined(TARGET_RAM)
    ota_mode_active = false;
    ota_completed = false;
    expected_chunk_offset = 0;
    total_image_size = 0;
#endif
}

bool mcu_midi_queue_tx_packet(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
{
    uint16_t next_head = (tx_head + 1) % MIDI_TX_RING_CAPACITY;
    if (next_head == tx_tail) {
        return false; /* Queue overflow */
    }

    tx_ring[tx_head].data[0] = b0;
    tx_ring[tx_head].data[1] = b1;
    tx_ring[tx_head].data[2] = b2;
    tx_ring[tx_head].data[3] = b3;
    __asm__ volatile("ssync; csync" ::: "memory");
    tx_head = next_head;

    mcu_midi_poll_tx();
    return true;
}

void mcu_midi_poll_tx(void)
{
    if (!hal_usb_is_configured()) {
        /* USB not attached/configured: discard queued packets to prevent buffer stalls */
        tx_tail = tx_head;
        tx_busy = false;
        return;
    }

    if (tx_busy) {
        return;
    }

    if (tx_head == tx_tail) {
        return; /* Nothing to send */
    }

    /* Batch up to 16 packets (64 bytes = max USB EP buffer size) */
    uint8_t batch_buf[64];
    size_t batch_bytes = 0;

    while (tx_head != tx_tail && batch_bytes + 4 <= sizeof(batch_buf)) {
        memcpy(&batch_buf[batch_bytes], tx_ring[tx_tail].data, 4);
        batch_bytes += 4;
        tx_tail = (tx_tail + 1) % MIDI_TX_RING_CAPACITY;
    }

    if (batch_bytes > 0) {
        tx_busy = true;
        if (!mcu_usb_ep4_tx(batch_buf, batch_bytes)) {
            tx_busy = false; /* Retry on next poll */
        }
    }
}

void mcu_midi_on_tx_complete(void)
{
    tx_busy = false;
    mcu_midi_poll_tx();
}

/* Transmit a raw byte sequence packaged into 4-byte USB-MIDI CIN frames */
static void send_raw_midi_stream(const uint8_t *raw, size_t len)
{
    size_t idx = 0;
    while (idx < len) {
        size_t rem = len - idx;
        if (rem >= 3) {
            uint8_t cin = (idx + 3 == len && raw[idx + 2] == 0xF7) ? 0x07 : 0x04;
            mcu_midi_queue_tx_packet(cin, raw[idx], raw[idx + 1], raw[idx + 2]);
            idx += 3;
        } else if (rem == 2) {
            uint8_t cin = (raw[idx + 1] == 0xF7) ? 0x06 : 0x04;
            mcu_midi_queue_tx_packet(cin, raw[idx], raw[idx + 1], 0);
            idx += 2;
        } else if (rem == 1) {
            uint8_t cin = (raw[idx] == 0xF7) ? 0x05 : 0x0F;
            mcu_midi_queue_tx_packet(cin, raw[idx], 0, 0);
            idx += 1;
        }
    }
}

/* -------------------------------------------------------------------------
 * JieLi LFSR Cipher & CRC16 Utilities for Firmware Container Detection
 * ------------------------------------------------------------------------- */
#if !defined(TARGET_RAM)
static uint16_t jl_enc_cipher_step(uint8_t *buf, size_t size, uint16_t key)
{
    for (size_t i = 0; i < size; i++) {
        buf[i] ^= (uint8_t)(key & 0xFF);
        key = (uint16_t)(((key << 1) ^ ((key & 0x8000) ? 0x1021 : 0)) & 0xFFFF);
    }
    return key;
}

static uint16_t jl_crc16_calc(const uint8_t *buf, size_t size)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < size; i++) {
        crc ^= (uint16_t)((uint16_t)buf[i] << 8);
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* Parse total container image size from chunk 0 if UFW or FWSC container */
__attribute__((unused)) static uint32_t parse_image_size(const uint8_t *chunk0, size_t len)
{
    if (len < 64) {
        return 0;
    }

    /* 1. Test direct UFW header */
    uint8_t hdr[64];
    memcpy(hdr, chunk0, 64);
    jl_enc_cipher_step(hdr, 64, 0xFFFF);
    uint16_t hcrc = (uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8);
    if (hcrc != 0 && jl_crc16_calc(&hdr[2], 62) == hcrc) {
        uint32_t imgsz = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                         ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
        return imgsz;
    }

    /* 2. Test M-VAVE FWSC header (watermark byte interleaved every 47 bytes) */
    if (len >= 65) {
        uint8_t fwsc_hdr[64];
        memcpy(&fwsc_hdr[0], &chunk0[0], 47);
        memcpy(&fwsc_hdr[47], &chunk0[48], 17);
        jl_enc_cipher_step(fwsc_hdr, 64, 0xFFFF);
        uint16_t hcrc2 = (uint16_t)fwsc_hdr[0] | ((uint16_t)fwsc_hdr[1] << 8);
        if (hcrc2 != 0 && jl_crc16_calc(&fwsc_hdr[2], 62) == hcrc2) {
            uint32_t imgsz = (uint32_t)fwsc_hdr[4] | ((uint32_t)fwsc_hdr[5] << 8) |
                             ((uint32_t)fwsc_hdr[6] << 16) | ((uint32_t)fwsc_hdr[7] << 24);
            return imgsz + 20; /* 20 interleaved watermark bytes */
        }
    }

    return 0;
}
#endif

static void trigger_bootloader_dfu(void)
{
#ifdef TARGET_MCU
    __asm__ volatile("cli");
    volatile char *pBootStatus = (volatile char *)0x01C7FD80;
    const char *tag = "usb_update_mode";
    for (int i = 0; i < 16; i++) {
        pBootStatus[i] = tag[i];
    }
    for (int i = 16; i < 64; i++) {
        pBootStatus[i] = 0;
    }
    __asm__ volatile("ssync; csync" ::: "memory");
    p33_soft_reset();
#endif
}

#if !defined(TARGET_RAM)
static void do_ota_reboot(void)
{
#ifdef TARGET_MCU
    /* Flush TX queue and wait briefly for USB packets to be retrieved */
    for (volatile int i = 0; i < 2000000; i++) {
        mcu_midi_poll_tx();
        if (tx_head == tx_tail && !tx_busy && i > 500000) {
            break;
        }
    }
    p33_soft_reset();
#endif
}

/* Send 17-byte SysEx-wrapped type-0x30 TLC ACK response */
static void send_tlc_ack(uint8_t subcmd, uint32_t offset_or_status, uint32_t payload_len)
{
    uint8_t frame[17];
    frame[0] = 0xF0;
    frame[1] = TLC_MAGIC_0;
    frame[2] = TLC_MAGIC_1;
    frame[3] = TLC_TYPE_CHUNK_WRITE;
    frame[4] = 0x08; /* LE24(8) length field */
    frame[5] = 0x00;
    frame[6] = 0x00;
    frame[7] = subcmd;
    frame[8] = (uint8_t)(offset_or_status & 0xFF);
    frame[9] = (uint8_t)((offset_or_status >> 8) & 0xFF);
    frame[10] = (uint8_t)((offset_or_status >> 16) & 0xFF);
    frame[11] = (uint8_t)((offset_or_status >> 24) & 0xFF);
    frame[12] = (uint8_t)(payload_len & 0xFF);
    frame[13] = (uint8_t)((payload_len >> 8) & 0xFF);
    frame[14] = (uint8_t)((payload_len >> 16) & 0xFF);

    /* Authoritative firmware checksum: sum(buf[7..14]) & 0xFF */
    uint32_t sum = 0;
    for (int i = 7; i < 15; i++) {
        sum += frame[i];
    }
    frame[15] = (uint8_t)(sum & 0xFF);
    frame[16] = 0xF7;

    send_raw_midi_stream(frame, sizeof(frame));
    mcu_midi_poll_tx();
}
#endif

/* -------------------------------------------------------------------------
 * TLC Host Protocol & Special SysEx Handlers
 * ------------------------------------------------------------------------- */
static void process_tlc_frame(const uint8_t *frame, size_t len)
{
    /* If frame is wrapped in SysEx (F0 ... F7), strip them */
    if (len >= 2 && frame[0] == 0xF0 && frame[len - 1] == 0xF7) {
        /* Check for 6-byte raw SysEx bootloader message: F0 22 24 35 [7F/7D] F7 */
        if (len == 6 && frame[1] == 0x22 && frame[2] == 0x24 && frame[3] == 0x35) {
            uint8_t sub = frame[4];
            if (sub == 0x7F) {
                mcu_ota_request_session();
                return;
            } else if (sub == 0x7D) {
                trigger_bootloader_dfu();
                return;
            }
        }
        mcu_ota_on_sysex_frame(frame + 1, len - 2);
        frame++;
        len -= 2;
    }

    if (len < 15) return;
    if (frame[0] != TLC_MAGIC_0 || frame[1] != TLC_MAGIC_1) return;

    uint32_t lenfield = frame[3] | ((uint32_t)frame[4] << 8) | ((uint32_t)frame[5] << 16);
    if (lenfield + 7 != len) return;

    /* Verify checksum over buf[6 .. last-1] */
    uint32_t sum = 0;
    for (size_t i = 6; i < len - 1; i++) {
        sum += frame[i];
    }
    if ((sum & 0xFF) != frame[len - 1]) {
        return; /* Checksum mismatch */
    }

    uint8_t type = frame[2];
    uint8_t subcmd = frame[6];

    switch (type) {
    case TLC_TYPE_STATUS_QUERY: {
        /* Type 0x11: Device Model & Version Query.
         * Echo 27-byte block wrapped in SysEx, matching stock 0x02057EE8 format:
         * "<Model>_<Version>\0" padded with zeros to 27 bytes, followed by
         * inverted checksum (~sum(payload[0..26]) & 0xFF).
         * M-UPGRADE.exe parses model before '_' and version integer after '_'.
         */
        extern const char FIRMWARE_VERSION[];
        uint8_t resp[36];
        resp[0] = 0xF0;
        resp[1] = 0x00;
        resp[2] = 0x59;
        resp[3] = TLC_TYPE_STATUS_QUERY;
        resp[4] = 0x1B; /* 27 bytes payload length (LE) */
        resp[5] = 0x00;
        resp[6] = 0x00;
        memset(&resp[7], 0, 27);
        strncpy((char *)&resp[7], FIRMWARE_VERSION, 26);
        uint8_t sum = 0;
        for (int i = 0; i < 27; i++) {
            sum += resp[7 + i];
        }
        resp[34] = (uint8_t)(~sum);
        resp[35] = 0xF7;
        send_raw_midi_stream(resp, sizeof(resp));
        mcu_midi_poll_tx();
        break;
    }

    case TLC_TYPE_MODE_QUERY: {
        /* Type 0x12: Mode ACK response matching stock EXT_RESERVE wrapped in SysEx */
        static const uint8_t mode_resp[] = {
            0xF0,
            0x00, 0x59, 0x12, 0x06, 0x00, 0x00, 0x00,
            'E', 'X', 'T', '_', 'R', 'E', 'S', 0x2A,
            0xF7
        };
        send_raw_midi_stream(mode_resp, sizeof(mode_resp));
        mcu_midi_poll_tx();
        break;
    }

    case TLC_TYPE_CHUNK_WRITE: {
#if defined(TARGET_RAM)
        /* In RAM-only build, flash writing is strictly prohibited.
         * Re-route any incoming write command directly to DFU reboot.
         */
        (void)subcmd;
        trigger_bootloader_dfu();
#else
        uint32_t offset = frame[7] | ((uint32_t)frame[8] << 8) |
                          ((uint32_t)frame[9] << 16) | ((uint32_t)frame[10] << 24);
        uint32_t plen = frame[11] | ((uint32_t)frame[12] << 8) | ((uint32_t)frame[13] << 16);
        const uint8_t *payload = &frame[14];

        if (plen == 0) {
            /* Empty poll query */
            if (ota_completed || (ota_mode_active && offset == TLC_STATE_RUNNING && expected_chunk_offset > 0)) {
                ota_completed = true;
                send_tlc_ack(subcmd, TLC_STATE_DONE, 0);
                do_ota_reboot();
            } else if (ota_mode_active) {
                /* Echo expected offset (starts at 0) */
                send_tlc_ack(subcmd, expected_chunk_offset, 0);
            } else {
                send_tlc_ack(subcmd, TLC_STATE_RUNNING, 0);
            }
        } else {
            /* Chunk data payload:
             * Enforce strict flash boundary protections.
             * Disallow writes below 0x4000 (uboot.boot SPL) or above 0x9C000 (VM, BTIF, USRTRIM, USR).
             */
            if (offset < FLASH_APP_START_ADDR || (offset + plen) > FLASH_APP_END_ADDR) {
                /* Out-of-bounds or raw container offset: reject unsafe write to protect bootloader and NVRAM */
                send_tlc_ack(subcmd, 0x5A06 /* Verification/boundary NAK */, 0);
                return;
            }

            if (ota_mode_active) {
                if (offset == expected_chunk_offset) {
#ifdef TARGET_MCU
                    /* Erase 4KB flash sector on boundary */
                    if ((offset % FLASH_SECTOR_SIZE) == 0) {
                        norflash_ioctl(NULL, 200 /* IOCTL_ERASE_SECTOR */, offset);
                    }
                    /* Write page payload to SPI flash */
                    norflash_write(NULL, (void *)payload, plen, offset);
#endif
                    expected_chunk_offset = offset + plen;

                    if (total_image_size > 0 && expected_chunk_offset >= total_image_size) {
                        ota_completed = true;
                        send_tlc_ack(subcmd, TLC_STATE_DONE, plen);
                        do_ota_reboot();
                    } else {
                        send_tlc_ack(subcmd, expected_chunk_offset, plen);
                    }
                } else {
                    /* Out of sync offset: NAK by echoing current expected offset */
                    send_tlc_ack(subcmd, expected_chunk_offset, 0);
                }
            } else {
                /* Non-OTA chunk write (e.g. preset banks): write to flash within safe boundary and ACK */
#ifdef TARGET_MCU
                if ((offset % FLASH_SECTOR_SIZE) == 0) {
                    norflash_ioctl(NULL, 200 /* IOCTL_ERASE_SECTOR */, offset);
                }
                norflash_write(NULL, (void *)payload, plen, offset);
#endif
                expected_chunk_offset = offset + plen;
                send_tlc_ack(subcmd, expected_chunk_offset, plen);
            }
        }
#endif
        break;
    }

    default:
        break;
    }
}

/* Check for 8-byte Bootloader SysEx Entry command */
static bool check_bootloader_sysex(const uint8_t *pkt, size_t len)
{
    /* Format: 04 F0 22 24 07 35 [7F/7D] F7 */
    if (len >= 8 &&
        pkt[0] == 0x04 && pkt[1] == 0xF0 && pkt[2] == 0x22 && pkt[3] == 0x24 &&
        pkt[4] == 0x07 && pkt[5] == 0x35 && pkt[7] == 0xF7) {
        uint8_t sub = pkt[6];
        if (sub == 0x7F) {
            mcu_ota_request_session();
            return true;
        } else if (sub == 0x7D) {
            trigger_bootloader_dfu();
            return true;
        }
    }
    return false;
}

void mcu_midi_process_rx_packet(const uint8_t *buffer, size_t len)
{
    if (!buffer || len < 4) return;

    /* Fast path: Bootloader SysEx check */
    if (check_bootloader_sysex(buffer, len)) {
        return;
    }

    for (size_t i = 0; i + 4 <= len; i += 4) {
        uint8_t cin = buffer[i] & 0x0F;
        uint8_t b1 = buffer[i + 1];
        uint8_t b2 = buffer[i + 2];
        uint8_t b3 = buffer[i + 3];

        switch (cin) {
        case 0x8: { /* Note Off */
            hal_synth_midi_event_from_isr(SPSC_EVT_NOTE_OFF, 0, b2, b3);
            break;
        }

        case 0x9: { /* Note On */
            if (b3 > 0) {
                hal_synth_midi_event_from_isr(SPSC_EVT_NOTE_ON, 0, b2, b3);
            } else {
                hal_synth_midi_event_from_isr(SPSC_EVT_NOTE_OFF, 0, b2, 64);
            }
            break;
        }

        case 0xB: { /* Control Change */
            if (b2 == MIDI_CC_MOD_WHEEL) {
                hal_synth_midi_event_from_isr(SPSC_EVT_CC, 0, 1, b3);
            } else if (b2 == MIDI_CC_VOLUME) {
                hal_synth_midi_event_from_isr(SPSC_EVT_CC, 0, 7, b3);
            } else if (b2 == MIDI_CC_ALL_NOTES_OFF || b2 == MIDI_CC_ALL_SOUND_OFF) {
                hal_synth_midi_event_from_isr(SPSC_EVT_ALL_NOTES_OFF, 0, 0, 0);
            } else {
                hal_synth_midi_event_from_isr(SPSC_EVT_CC, 0, b2, b3);
            }
            break;
        }

        case 0xC: { /* Program Change */
            hal_synth_midi_event_from_isr(SPSC_EVT_PROGRAM_CHANGE, 0, 0, b2);
            break;
        }

        case 0xD: { /* Channel Pressure / Aftertouch */
            hal_synth_midi_event_from_isr(SPSC_EVT_AFTERTOUCH, 0, b2, 0);
            break;
        }

        case 0xE: { /* Pitch Bend */
            int16_t bend = (int16_t)(((int)b3 << 7) | b2) - 8192;
            hal_synth_midi_event_from_isr(SPSC_EVT_PITCH_BEND, 0, (uint8_t)(bend & 0xFF), (uint8_t)((bend >> 8) & 0xFF));
            break;
        }

        /* SysEx Accumulation for TLC protocol */
        case 0x4: { /* SysEx start or continue (3 bytes) */
            if (b1 == 0xF0) {
                sysex_len = 0;
                sysex_in_progress = true;
            }
            if (sysex_in_progress && sysex_len + 3 <= SYSEX_ACCUM_MAX) {
                sysex_buf[sysex_len++] = b1;
                sysex_buf[sysex_len++] = b2;
                sysex_buf[sysex_len++] = b3;
            }
            break;
        }

        case 0x5:   /* SysEx ends with 1 byte */
        case 0x6:   /* SysEx ends with 2 bytes */
        case 0x7: { /* SysEx ends with 3 bytes */
            uint8_t count = (cin == 0x5) ? 1 : ((cin == 0x6) ? 2 : 3);
            const uint8_t *src = &buffer[i + 1];
            for (uint8_t c = 0; c < count; c++) {
                if (sysex_len < SYSEX_ACCUM_MAX) {
                    sysex_buf[sysex_len++] = src[c];
                }
            }
            if (sysex_in_progress) {
                sysex_in_progress = false;
                process_tlc_frame(sysex_buf, sysex_len);
            }
            break;
        }

        default:
            break;
        }
    }
}

/* -------------------------------------------------------------------------
 * HAL Implementation Functions
 * ------------------------------------------------------------------------- */
int hal_midi_init(void)
{
    mcu_midi_init();
    return 0;
}

void hal_midi_send_note_on(uint8_t ch, uint8_t note, uint8_t velocity)
{
    uint8_t cin = 0x09;
    uint8_t status = (uint8_t)(MIDI_STATUS_NOTE_ON | (ch & 0x0F));
    mcu_midi_queue_tx_packet(cin, status, note & 0x7F, velocity & 0x7F);
}

void hal_midi_send_note_off(uint8_t ch, uint8_t note, uint8_t velocity)
{
    uint8_t cin = 0x08;
    uint8_t status = (uint8_t)(MIDI_STATUS_NOTE_OFF | (ch & 0x0F));
    mcu_midi_queue_tx_packet(cin, status, note & 0x7F, velocity & 0x7F);
}

void hal_midi_send_cc(uint8_t ch, uint8_t cc, uint8_t value)
{
    uint8_t cin = 0x0B;
    uint8_t status = (uint8_t)(MIDI_STATUS_CONTROL_CHANGE | (ch & 0x0F));
    mcu_midi_queue_tx_packet(cin, status, cc & 0x7F, value & 0x7F);
}

void hal_midi_send_pitch_bend(uint8_t ch, uint16_t bend_14bit)
{
    uint8_t cin = 0x0E;
    uint8_t status = (uint8_t)(MIDI_STATUS_PITCH_BEND | (ch & 0x0F));
    uint8_t lsb = (uint8_t)(bend_14bit & 0x7F);
    uint8_t msb = (uint8_t)((bend_14bit >> 7) & 0x7F);
    mcu_midi_queue_tx_packet(cin, status, lsb, msb);
}

void hal_midi_send_channel_pressure(uint8_t ch, uint8_t pressure)
{
    uint8_t cin = 0x0D;
    uint8_t status = (uint8_t)(MIDI_STATUS_CHANNEL_PRESSURE | (ch & 0x0F));
    mcu_midi_queue_tx_packet(cin, status, pressure & 0x7F, 0);
}

void hal_midi_send_poly_pressure(uint8_t ch, uint8_t note, uint8_t pressure)
{
    uint8_t cin = 0x0A;
    uint8_t status = (uint8_t)(MIDI_STATUS_POLY_KEY_PRESSURE | (ch & 0x0F));
    mcu_midi_queue_tx_packet(cin, status, note & 0x7F, pressure & 0x7F);
}

void hal_midi_send_program_change(uint8_t ch, uint8_t program)
{
    uint8_t cin = 0x0C;
    uint8_t status = (uint8_t)(MIDI_STATUS_PROGRAM_CHANGE | (ch & 0x0F));
    mcu_midi_queue_tx_packet(cin, status, program & 0x7F, 0);
}

void hal_midi_send_raw(const uint8_t *data, size_t len)
{
    send_raw_midi_stream(data, len);
}
