/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_ota.c
 * Stage 1 OTA Update Engine for SMK-37 Pro
 *
 * Implements the interactive pull-based M-VAVE OTA protocol:
 *   1. Answers status query 0x11 with identity "SMK-37 Pro_001" (Pack7 encoded)
 *   2. On F0 22 24 35 7F F7, initiates ota_session() in thread context:
 *      - Halts audio DSP and secondary CPU core
 *      - Pulls UFW header and update loader (ota.bin) using cmd 0x30 requests
 *      - Stages ota.bin into scratch flash at 0x000E0000
 *      - Verifies CRC against package container
 *      - Obtains host finish confirmation ("success" at 0xE0000000)
 *      - Writes 112-byte UPDATA_PARM record (flash 0x000E4F00 + RAM 0x01C7FD88)
 *      - Resets CPU into SPL (uboot.boot), which launches the Stage 2 RAM loader
 */

#include "mcu_ota.h"
#include "mcu_midi.h"
#include "ota/ota_common.h"
#include "hal_time.h"
#include "hal_synth.h"
#include "st7789.h"
#include "asm/WL82.h"
#include <string.h>

extern void wdt_clear(void);

#define FLASH_IN_RANGE(off, len, start, total) \
    ((uint32_t)(off) >= (start) && (uint32_t)(off) <= ((start) + (total)) && (uint32_t)(len) <= (((start) + (total)) - (uint32_t)(off)))

#define FLASH_STAGING_VALID(off, len) \
    (FLASH_IN_RANGE(off, len, OTA_STAGING_ADDR, OTA_STAGING_LEN))

typedef struct {
    uint32_t saved_sfc_con;
    uint32_t saved_spi_con;
} FlashControllerState;

/* -------------------------------------------------------------------------
 * Low-level Flash Driver in RAM (.volatile_ram_code)
 * ------------------------------------------------------------------------- */
#define MCU_RAM_FUNCTION  __attribute__((section(".volatile_ram_code"), noinline, used))
#define MCU_INLINE        static inline __attribute__((always_inline))

static inline void *mcu_volatile_ptr(void *ptr) { void *volatile p = ptr; return p; }
#define MCU_RAM_CALL(fn) ((__typeof__(&fn))mcu_volatile_ptr((void *)&fn))

MCU_INLINE void flash_sync_pipeline(void) { __asm__ volatile("csync" ::: "memory"); }
MCU_INLINE void flash_wait_cache_idle(void) { while (!(corex2(0)->CACHE_CON & 0x4000u)) ; }
MCU_INLINE void flash_delay_cycles(uint32_t count) { volatile uint32_t c = count; while (c--) ; }

MCU_INLINE void flash_set_cs(bool assert_low)
{
    JL_PORTD->DIR &= ~1u;
    if (assert_low) {
        JL_PORTD->OUT &= ~1u;
    } else {
        JL_PORTD->OUT |= 1u;
    }
    flash_delay_cycles(12);
}

MCU_INLINE int flash_wait_spi_completion(void)
{
    uint32_t timeout = 50000000u;
    flash_sync_pipeline();
    while (!(JL_SPI0->CON & 0x8000u)) {
        if (--timeout == 0) break;
    }
    JL_SPI0->CON |= 0x4000u;
    return (timeout > 0) ? 0 : -1;
}

MCU_INLINE void flash_tx_byte(uint8_t byte)
{
    JL_SPI0->CON |= (1u << 3);
    JL_SPI0->CON &= ~(1u << 12);
    JL_SPI0->BUF = byte;
    flash_wait_spi_completion();
    JL_SPI0->CON &= ~(1u << 3);
}

MCU_INLINE uint8_t flash_rx_byte(void)
{
    uint8_t val;
    JL_SPI0->CON |= (1u << 3);
    JL_SPI0->CON |= (1u << 12);
    JL_SPI0->BUF = 0xFFu;
    flash_wait_spi_completion();
    val = (uint8_t)JL_SPI0->BUF;
    JL_SPI0->CON &= ~(1u << 3);
    return val;
}

MCU_INLINE void flash_send_address(uint32_t addr)
{
    flash_tx_byte((uint8_t)(addr >> 16));
    flash_tx_byte((uint8_t)(addr >> 8));
    flash_tx_byte((uint8_t)addr);
}

MCU_INLINE uint8_t flash_read_status_reg(uint8_t opcode)
{
    uint8_t status;
    flash_set_cs(true);
    flash_tx_byte(opcode);
    status = flash_rx_byte();
    flash_set_cs(false);
    return status;
}

MCU_INLINE void flash_send_write_enable(void)
{
    flash_set_cs(true);
    flash_tx_byte(FLASH_CMD_WREN);
    flash_set_cs(false);
}

MCU_INLINE int flash_acquire_bus(FlashControllerState *state)
{
    flash_wait_cache_idle();
    state->saved_sfc_con = JL_SFC->CON;
    state->saved_spi_con = JL_SPI0->CON;

    uint32_t sfc_mode = (state->saved_sfc_con >> 8) & 0x0Fu;
    if (sfc_mode == 6u || sfc_mode == 7u) {
        return -1;
    }

    flash_sync_pipeline();
    while ((int32_t)JL_SFC->CON < 0) ;
    JL_SFC->CON = 0;
    JL_IOMAP->CON0 &= ~0x20u;
    JL_SPI0->CON = 0;
    JL_SPI0->CON = 0x1029u;
    flash_sync_pipeline();
    return 0;
}

MCU_INLINE void flash_release_bus(const FlashControllerState *state)
{
    JL_SPI0->CON = state->saved_spi_con;
    JL_IOMAP->CON0 |= 0x20u;
    flash_sync_pipeline();
    while ((int32_t)JL_SFC->CON < 0) ;
    JL_SFC->CON = state->saved_sfc_con;
    flash_sync_pipeline();
}

MCU_INLINE void flash_invalidate_cache_range(uint32_t offset, uint32_t len)
{
    uint32_t addr = (0x02000000u + offset - 0x4000u) & ~31u;
    uint32_t end_addr = (0x02000000u + offset - 0x4000u) + len;
    flash_wait_cache_idle();
    for (; addr < end_addr; addr += 32u) {
        flash_sync_pipeline();
        __asm__ volatile("flushinv [%0]" :: "r"(addr) : "memory");
        flash_sync_pipeline();
        flash_wait_cache_idle();
    }
}

MCU_INLINE int flash_poll_ready(uint32_t max_wait_us, uint32_t *elapsed_us)
{
    uint32_t start_tick = JL_TIMER4->CNT;
    for (;;) {
        uint32_t elapsed = (JL_TIMER4->CNT - start_tick) / 24u;
        if (!(flash_read_status_reg(FLASH_CMD_RDSR) & 0x01u)) {
            if (elapsed_us) *elapsed_us = elapsed;
            return 0;
        }
        if (elapsed > max_wait_us) {
            if (elapsed_us) *elapsed_us = elapsed;
            return -4;
        }
    }
}

static MCU_RAM_FUNCTION int flash_ram_erase_sector(uint32_t offset, uint32_t *elapsed_us)
{
    FlashControllerState state;
    int rc;

    if (!FLASH_STAGING_VALID(offset, FLASH_SECTOR_SIZE) || (offset & (FLASH_SECTOR_SIZE - 1u))) {
        return -1;
    }
    if (flash_acquire_bus(&state) != 0) {
        return -2;
    }

    flash_send_write_enable();
    if (!(flash_read_status_reg(FLASH_CMD_RDSR) & 0x02u)) {
        rc = -3;
    } else {
        flash_set_cs(true);
        flash_tx_byte(FLASH_CMD_SECTOR_ERASE);
        flash_send_address(offset);
        flash_set_cs(false);
        flash_invalidate_cache_range(offset, FLASH_SECTOR_SIZE);
        rc = flash_poll_ready(500000u, elapsed_us);
    }

    flash_release_bus(&state);
    return rc;
}

static MCU_RAM_FUNCTION int flash_ram_program_page(uint32_t offset, const uint8_t *src, uint32_t len, uint32_t *elapsed_us)
{
    FlashControllerState state;
    int rc;

    if (!FLASH_STAGING_VALID(offset, len) || len == 0 || len > FLASH_PAGE_SIZE || ((offset & (FLASH_PAGE_SIZE - 1u)) + len) > FLASH_PAGE_SIZE) {
        return -1;
    }
    if (flash_acquire_bus(&state) != 0) {
        return -2;
    }

    flash_send_write_enable();
    if (!(flash_read_status_reg(FLASH_CMD_RDSR) & 0x02u)) {
        rc = -3;
    } else {
        flash_set_cs(true);
        flash_tx_byte(FLASH_CMD_PAGE_PROGRAM);
        flash_send_address(offset);
        for (uint32_t i = 0; i < len; i++) {
            flash_tx_byte(src[i]);
        }
        flash_set_cs(false);
        flash_invalidate_cache_range(offset, len);
        rc = flash_poll_ready(10000u, elapsed_us);
    }

    flash_release_bus(&state);
    return rc;
}

static MCU_RAM_FUNCTION int flash_ram_read_data(uint32_t offset, uint8_t *dest, uint32_t len)
{
    FlashControllerState state;

    if (flash_acquire_bus(&state) != 0) {
        return -2;
    }

    flash_set_cs(true);
    flash_tx_byte(FLASH_CMD_FAST_READ);
    flash_send_address(offset);
    flash_tx_byte(0xFFu);
    for (uint32_t i = 0; i < len; i++) {
        dest[i] = flash_rx_byte();
    }
    flash_set_cs(false);

    flash_release_bus(&state);
    return 0;
}

static inline uint32_t flash_irq_disable(void) { __asm__ volatile("cli" ::: "memory"); return 0; }
static inline void flash_irq_enable(uint32_t flags) { (void)flags; __asm__ volatile("csync\n\tsti" ::: "memory"); }

static int ota_erase(uint32_t offset)
{
    uint32_t elapsed;
    uint32_t flags = flash_irq_disable();
    int rc = MCU_RAM_CALL(flash_ram_erase_sector)(offset, &elapsed);
    flash_irq_enable(flags);
    return rc;
}

static int ota_prog(uint32_t offset, const void *data, uint32_t len)
{
    const uint8_t *src = (const uint8_t *)data;
    uint32_t elapsed;
    while (len > 0) {
        uint32_t chunk = FLASH_PAGE_SIZE - (offset & (FLASH_PAGE_SIZE - 1u));
        if (chunk > len) {
            chunk = len;
        }
        uint32_t flags = flash_irq_disable();
        int rc = MCU_RAM_CALL(flash_ram_program_page)(offset, src, chunk, &elapsed);
        flash_irq_enable(flags);
        if (rc != 0) {
            return rc;
        }
        offset += chunk;
        src += chunk;
        len -= chunk;
    }
    return 0;
}

static int ota_fread(uint32_t offset, void *dest, uint32_t len)
{
    uint32_t flags = flash_irq_disable();
    int rc = MCU_RAM_CALL(flash_ram_read_data)(offset, (uint8_t *)dest, len);
    flash_irq_enable(flags);
    return rc;
}

/* -------------------------------------------------------------------------
 * SysEx and TLC Wire Protocol
 * ------------------------------------------------------------------------- */
/* Transmit F0..F7 wrapped raw MIDI stream */
static int ota_wire_send(const uint8_t *raw, uint32_t len)
{
    size_t idx = 0;
    while (idx < len) {
        size_t rem = len - idx;
        if (rem >= 3) {
            uint8_t cin = (idx + 3 == len && raw[idx + 2] == 0xF7) ? 0x07 : 0x04;
            if (!mcu_midi_queue_tx_packet(cin, raw[idx], raw[idx + 1], raw[idx + 2])) {
                return -1;
            }
            idx += 3;
        } else if (rem == 2) {
            uint8_t cin = (raw[idx + 1] == 0xF7) ? 0x06 : 0x04;
            if (!mcu_midi_queue_tx_packet(cin, raw[idx], raw[idx + 1], 0)) {
                return -1;
            }
            idx += 2;
        } else if (rem == 1) {
            uint8_t cin = (raw[idx] == 0xF7) ? 0x05 : 0x0F;
            if (!mcu_midi_queue_tx_packet(cin, raw[idx], 0, 0)) {
                return -1;
            }
            idx += 1;
        }
    }
    mcu_midi_poll_tx();
    return 0;
}

static int ota_send_msg(uint32_t cmd, const uint8_t *body, uint32_t n)
{
    uint8_t msg[8 + 32], wire[2 + 64];
    uint32_t i, s = 0, w;
    msg[0] = TLC_MAGIC_0;
    msg[1] = TLC_MAGIC_1;
    msg[2] = (uint8_t)cmd;
    msg[3] = (uint8_t)n;
    msg[4] = 0;
    msg[5] = 0;
    for (i = 0; i < n; i++) {
        msg[6 + i] = body[i];
        s += body[i];
    }
    msg[6 + n] = (uint8_t)~s;
    wire[0] = 0xF0;
    w = 1u + ota_encode_pack7(msg, 7u + n, wire + 1);
    wire[w++] = 0xF7;
    return ota_wire_send(wire, w);
}

void mcu_ota_reply_identity(void)
{
    static const char ID[] = IDENTITY_STRING_STAGE1;
    uint8_t id[27];
    for (uint32_t i = 0; i < sizeof(id); i++) {
        id[i] = (i < sizeof(ID) - 1u) ? (uint8_t)ID[i] : 0;
    }
    ota_send_msg(TLC_CMD_STATUS_QUERY, id, sizeof(id));
}

/* Incoming SysEx Frame State */
static uint8_t rx_frame[OTA_FRAME_MAX];
static uint32_t rx_frame_len = 0;
static volatile bool rx_ready = false;
static uint8_t dec_buf[OTA_FRAME_MAX];

void mcu_ota_on_sysex_frame(const uint8_t *frame_payload, size_t len)
{
    if (len > sizeof(rx_frame)) {
        return;
    }
    memcpy(rx_frame, frame_payload, len);
    rx_frame_len = len;
    rx_ready = true;
}

static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!rx_ready) {
        return 0;
    }
    *p = rx_frame;
    *n = rx_frame_len;
    return 1;
}

static void ota_frame_done(void)
{
    rx_ready = false;
}

static uint32_t ota_take(void)
{
    const uint8_t *p;
    uint32_t n, d, s = 0, i;
    if (!ota_frame_get(&p, &n)) {
        return 0;
    }
    d = ota_decode_unpack7(p, n, dec_buf, sizeof(dec_buf));
    ota_frame_done();
    if (d < 7u || d > sizeof(dec_buf) || dec_buf[0] != TLC_MAGIC_0 || dec_buf[1] != TLC_MAGIC_1 || ota_rd24(dec_buf + 3) + 7u != d) {
        return 0;
    }
    for (i = 6; i + 1u < d; i++) {
        s += dec_buf[i];
    }
    if ((uint8_t)~s != dec_buf[d - 1u]) {
        return 0;
    }
    return d;
}

static void ota_idle(void)
{
    wdt_clear();
    mcu_midi_poll_tx();
}

static int ota_read(uint32_t addr, uint8_t *dst, uint32_t len)
{
    uint8_t q[8];
    uint32_t tries, t, d, i;
    q[0] = 0;
    ota_wr32(q + 1, addr);
    q[5] = (uint8_t)len;
    q[6] = (uint8_t)(len >> 8);
    q[7] = 0;

    for (tries = 0; tries < 3u; tries++) {
        if (ota_send_msg(TLC_CMD_CHUNK_REQUEST, q, 8)) {
            return -1;
        }
        for (t = hal_get_ms(); hal_get_ms() - t < 5000u;) {
            ota_idle();
            d = ota_take();
            if (!d) continue;
            if (d == 7u && dec_buf[2] == TLC_CMD_STATUS_QUERY) {
                mcu_ota_reply_identity();
                continue;
            }
            if (d == len + 15u && dec_buf[2] == TLC_CMD_CHUNK_REQUEST && ota_rd24(dec_buf + 3) == len + 8u &&
                ota_rd32(dec_buf + 7) == addr && ota_rd24(dec_buf + 11) == len) {
                for (i = 0; i < len; i++) {
                    dst[i] = dec_buf[14 + i];
                }
                return 0;
            }
        }
    }
    return -1;
}

static int ota_stage_head(uint32_t len)
{
    static const char NAME[16] = "LOADER.BIN";
    uint8_t h[32];
    uint32_t i;
    for (i = 0; i < 32u; i++) h[i] = 0;
    ota_wr16(h + 2, 0xFFFFu);
    ota_wr32(h + 4, OTA_STAGING_ADDR);
    ota_wr32(h + 8, len + 32u);
    h[12] = 0x41u;
    h[13] = 0xFFu;
    ota_wr16(h + 14, 0xFFFFu);
    for (i = 0; i < 16u; i++) h[16 + i] = (uint8_t)NAME[i];
    ota_wr16(h, ota_calc_crc16(h + 2, 30, 0));
    return ota_prog(OTA_STAGING_ADDR, h, 32);
}

static int ota_ufw(uint8_t *hdr, uint32_t *fl_off, uint32_t *fl_len, uint32_t *ota_off, uint32_t *ota_len)
{
    uint32_t i, nent;
    *fl_off = *fl_len = *ota_off = *ota_len = 0;
    if (ota_read(0, hdr, 512) || ota_read(512, hdr + 512, 512)) return -1;
    ota_scramble_payload(hdr, 0x40);
    nent = ota_rd16(hdr + 8);
    if (ota_calc_crc16(hdr + 2, 0x3E, 0) != ota_rd16(hdr) || nent == 0 || nent > 11u ||
        ota_calc_crc16(hdr + 0x40, nent * 0x50u, 0) != ota_rd16(hdr + 2)) {
        return -2;
    }
    for (i = 0; i < nent; i++) {
        uint8_t *e = hdr + 0x40 + i * 0x50u;
        ota_scramble_payload(e, 0x50);
        if (ota_rd16(e) == 0) {
            *fl_off = ota_rd32(e + 8);
            *fl_len = ota_rd32(e + 12);
        }
        if (ota_rd16(e) == 100) {
            *ota_off = ota_rd32(e + 8);
            *ota_len = ota_rd32(e + 12);
        }
    }
    return 0;
}

static void ota_unstage(void)
{
    for (uint32_t i = 0; i < OTA_STAGING_LEN; i += FLASH_SECTOR_SIZE) {
        ota_erase(OTA_STAGING_ADDR + i);
    }
}

static int ota_stage(void)
{
    static uint8_t hdr[0x400], b[512];
    uint32_t i, n, ota_off, ota_len, fl_off, fl_len, c, len;
    int rc;

    /* 1. Read and parse UFW header */
    if ((rc = ota_ufw(hdr, &fl_off, &fl_len, &ota_off, &ota_len)) != 0) return rc;
    if (!fl_off || !ota_off) return -3;
    if (fl_len != FLASH_APP_END && fl_len != FLASH_APP_END_STOCK) return -15;

    /* 2. Validate update loader */
    if (ota_read(ota_off, b, 512)) return -6;
    len = ota_rd32(b + 8);
    if (ota_calc_crc16(b + 2, 30, 0) != ota_rd16(b) || ota_rd32(b + 4) != 0x20u || b[12] != 0x41u ||
        ota_len != len + 0x20u || len > OTA_STAGING_LEN - 0x200u || ota_calc_crc16(b + 0x22, 30, 0) != ota_rd16(b + 0x20) ||
        ota_rd32(b + 0x20 + 8) != 0x01C0A800u || ota_rd32(b + 0x20 + 4) > 0x14000u) {
        return -7;
    }

    /* 3. Stage the package's loader: erase, body, verify, head */
    if (ota_read(ota_off, b, 512)) return -6;
    c = ota_rd16(b + 2);

    for (i = 0; i < OTA_STAGING_LEN; i += FLASH_SECTOR_SIZE) {
        if (ota_erase(OTA_STAGING_ADDR + i)) return -8;
    }

    {
        uint32_t crc = 0;
        for (i = 0; i < len; i += n) {
            n = (len - i > 512u) ? 512u : (len - i);
            if (ota_read(ota_off + 0x20u + i, b, n)) return -9;
            crc = ota_calc_crc16(b, n, crc);
            if (ota_prog(OTA_STAGING_ADDR + 32u + i, b, n)) return -10;
        }
        if (crc != c) return -11;

        for (i = 0, crc = 0; i < len; i += n) {
            n = (len - i > 512u) ? 512u : (len - i);
            if (ota_fread(OTA_STAGING_ADDR + 32u + i, b, n)) return -11;
            crc = ota_calc_crc16(b, n, crc);
        }
        if (crc != c) return -11;
    }

    if (ota_stage_head(len)) return -12;

    /* 4. Host finish check */
    for (i = 0; i < 4u; i++) {
        if (!ota_read(TLC_ADDR_STAGING_READY, b, 8) && memcmp(b, "success", 7) == 0) {
            return 0;
        }
    }
    return -13;
}

static volatile bool ota_session_requested = false;

void mcu_ota_request_session(void)
{
    ota_session_requested = true;
}

static void ota_commit(const uint8_t *parm)
{
    (void)parm;
    /* Halt secondary CPU core */
    corex2(0)->C1_CON |= 0x2u;
    corex2(0)->C1_CON &= ~0x8u;
    __asm__ volatile("csync" ::: "memory");

    /* Detach USB */
    JL_USB->CON0 = 0;
    JL_USB_IO->CON0 = 0x0Cu;
    for (volatile int d = 0; d < 1000000; d++) {
        __asm__ volatile("nop");
    }

    /* Core soft reset */
    JL_POWER->CON |= 0x10u;
    for (;;) ;
}

static int do_mcu_ota_session(void)
{
    static uint8_t parm[OTA_PARM_LEN];
    uint8_t back[OTA_PARM_LEN];
    int rc;

    /* Visual update screen */
    st7789_fill_screen(COLOR_BLACK);

    /* Pause sound output */
    hal_synth_all_notes_off();

    rc = ota_stage();
    if (rc != 0) {
        ota_unstage();
        return rc;
    }

    /* Construct UPDATA_PARM */
    memset(parm, 0, sizeof(parm));
    ota_wr16(parm + 2, 0x5A0Du);
    ota_wr16(parm + 4, 0x5A01u);
    ota_wr16(parm + 6, 0x5441u); /* 'AT' */
    strncpy((char *)&parm[8], IDENTITY_STRING_STAGE2, 31);
    ota_wr32(parm + 72, OTA_STAGING_ADDR);
    ota_wr16(parm, ota_calc_crc16(parm + 2, 78, 0));

    /* Write to flash backup first */
    if (ota_prog(OTA_PARM_FLASH_ADDR, parm, sizeof(parm)) ||
        ota_fread(OTA_PARM_FLASH_ADDR, back, sizeof(back)) ||
        memcmp(back, parm, sizeof(parm)) != 0) {
        ota_unstage();
        return -14;
    }

    /* Write to retention RAM */
    for (uint32_t i = 0; i < OTA_PARM_LEN; i++) {
        OTA_PARM_RAM_ADDR[i] = parm[i];
    }

    ota_commit(parm);
    return 0;
}

void mcu_ota_service(void)
{
    uint32_t d = ota_take();
    if (d == 7u && dec_buf[2] == TLC_CMD_STATUS_QUERY) {
        mcu_ota_reply_identity();
    }
}

void mcu_ota_check_pending(void)
{
    if (ota_session_requested) {
        ota_session_requested = false;
        do_mcu_ota_session();
    } else {
        mcu_ota_service();
    }
}

int mcu_ota_boot_cleanup(void)
{
    uint8_t a[4], r[8];
    uint32_t i, used = 0;
    if (ota_fread(OTA_STAGING_ADDR, a, 4) || ota_fread(OTA_PARM_FLASH_ADDR, r, 8)) {
        return -1;
    }
    for (i = 0; i < 4u; i++) {
        used |= (a[i] != 0xFFu);
    }
    for (i = 0; i < 8u; i++) {
        used |= (r[i] != 0xFFu);
    }
    if (used) {
        ota_unstage();
    }
    return (int)used;
}
