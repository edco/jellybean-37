/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef OTA_FLASH_H
#define OTA_FLASH_H

#include "ota_common.h"
#include "ota_sys.h"
#include "ota_time.h"

static inline void flash_cs(bool assert_active)
{
    JL_PORTD->DIR &= ~1u; /* Ensure PD0 is output */
    if (assert_active) {
        JL_PORTD->OUT &= ~1u; /* Active low */
    } else {
        JL_PORTD->OUT |= 1u;  /* Inactive high */
    }
    time_delay_cycles(12);
}

static inline void flash_spi_wait_done(void)
{
    sys_sync();
    while (!(JL_SPI0->CON & 0x8000u)) {
    }
    JL_SPI0->CON |= 0x4000u; /* Clear completion flag */
}

static inline void flash_spi_tx(uint8_t byte)
{
    JL_SPI0->CON |= (1u << 3);       /* Transmit mode */
    JL_SPI0->CON &= ~(1u << 12);     /* Normal direction */
    JL_SPI0->BUF = byte;
    flash_spi_wait_done();
    JL_SPI0->CON &= ~(1u << 3);
}

static inline uint8_t flash_spi_rx(void)
{
    uint8_t val;
    JL_SPI0->CON |= (1u << 3);       /* Enable clock generation */
    JL_SPI0->CON |= (1u << 12);      /* Receive direction */
    JL_SPI0->BUF = 0xFFu;
    flash_spi_wait_done();
    val = (uint8_t)JL_SPI0->BUF;
    JL_SPI0->CON &= ~(1u << 3);
    return val;
}

static inline void flash_tx_address(uint32_t addr)
{
    flash_spi_tx((uint8_t)(addr >> 16));
    flash_spi_tx((uint8_t)(addr >> 8));
    flash_spi_tx((uint8_t)addr);
}

static inline uint8_t flash_read_status(void)
{
    uint8_t status;
    flash_cs(true);
    flash_spi_tx(FLASH_CMD_RDSR);
    status = flash_spi_rx();
    flash_cs(false);
    return status;
}

static inline void flash_write_enable(void)
{
    flash_cs(true);
    flash_spi_tx(FLASH_CMD_WREN);
    flash_cs(false);
}

static inline int flash_wait_ready(uint32_t timeout_ms)
{
    uint32_t start_ms = time_get_ms();
    while (flash_read_status() & 0x01u) {
        if ((time_get_ms() - start_ms) > timeout_ms) {
            return -1;
        }
    }
    return 0;
}

static inline void flash_init_hardware(void)
{
    /* Ensure cache is idle */
    while (!(corex2(0)->CACHE_CON & 0x4000u)) {
    }
    sys_sync();

    /* Disable SFC hardware */
    JL_SFC->CON = 0;

    /* Remap SPI0 pins to flash interface */
    JL_IOMAP->CON0 &= ~0x20u;

    /* Configure SPI0 in Master Mode */
    JL_SPI0->CON = 0;
    JL_SPI0->CON = 0x1029u; /* Baud divider and clock polarity */
    sys_sync();

    flash_cs(false);
}

static inline int flash_erase_sector(uint32_t addr)
{
    flash_write_enable();
    if (!(flash_read_status() & 0x02u)) {
        return -1; /* WEL not set */
    }

    flash_cs(true);
    flash_spi_tx(FLASH_CMD_SECTOR_ERASE);
    flash_tx_address(addr);
    flash_cs(false);

    return flash_wait_ready(500); /* Up to 500ms for sector erase */
}

static inline int flash_program_chunk(uint32_t addr, const uint8_t *src, uint32_t len)
{
    if (len == 0 || len > FLASH_PAGE_SIZE) {
        return -1;
    }

    flash_write_enable();
    if (!(flash_read_status() & 0x02u)) {
        return -1;
    }

    flash_cs(true);
    flash_spi_tx(FLASH_CMD_PAGE_PROGRAM);
    flash_tx_address(addr);
    for (uint32_t i = 0; i < len; i++) {
        flash_spi_tx(src[i]);
    }
    flash_cs(false);

    return flash_wait_ready(20); /* Page program typically < 5ms */
}

static inline int flash_read_bytes(uint32_t addr, uint8_t *dest, uint32_t len)
{
    flash_cs(true);
    flash_spi_tx(FLASH_CMD_FAST_READ);
    flash_tx_address(addr);
    flash_spi_tx(0xFFu); /* Dummy cycle for fast read */
    for (uint32_t i = 0; i < len; i++) {
        dest[i] = flash_spi_rx();
    }
    flash_cs(false);
    return 0;
}

#endif /* OTA_FLASH_H */
