/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file st7789.c
 * ST7789 240x240 RGB565 LCD Hardware Driver for JieLi AC791N (SMK-37 Pro)
 */

#include "st7789.h"
#include "hal_time.h"
#include "asm/WL82.h"
#include "driver/device/gpio.h"

/* GPIO Pin assignments */
#define PIN_DC          IO_PORTB_05  /* PB5: 0=Command, 1=Data */
#define PIN_RST         IO_PORTB_06  /* PB6: Active Low Reset */
#define PIN_BL          IO_PORTB_07  /* PB7: Backlight Enable (High=ON) */
#define PIN_CS          IO_PORTC_08  /* PC8: Chip Select (Low=Active) */


static void delay_ms(uint32_t ms)
{
    hal_delay_ms(ms);
}

static void st7789_write_cmd_params(uint8_t cmd, const uint8_t *params, uint32_t count)
{
    gpio_direction_output(PIN_CS, 0);   /* CS = 0 (Active for entire transaction) */
    gpio_direction_output(PIN_DC, 0);   /* DC = 0 (Command) */

    JL_SPI1->CON |= 0x4000;         /* Clear pending */
    JL_SPI1->BUF = cmd;             /* Send command byte */
    while ((JL_SPI1->CON & 0x8000) == 0); /* Wait until command byte is fully clocked out */
    JL_SPI1->CON |= 0x4000;         /* Clear pending */

    if (params && count > 0) {
        gpio_direction_output(PIN_DC, 1);/* DC = 1 (Parameters) - switched only after command transmission completes */

        for (uint32_t i = 0; i < count; i++) {
            JL_SPI1->BUF = params[i];
            while ((JL_SPI1->CON & 0x8000) == 0); /* Wait until parameter byte is fully clocked out */
            JL_SPI1->CON |= 0x4000;
        }
    }

    gpio_direction_output(PIN_CS, 1);    /* CS = 1 (Transaction complete) */
}

static void st7789_write_cmd(uint8_t cmd)
{
    st7789_write_cmd_params(cmd, NULL, 0);
}

void st7789_prepare_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t caset[4] = {
        (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF)
    };
    uint8_t raset[4] = {
        (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF)
    };

    st7789_write_cmd_params(0x2A, caset, sizeof(caset)); /* Column Address Set */
    st7789_write_cmd_params(0x2B, raset, sizeof(raset)); /* Row Address Set */
}

void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    st7789_prepare_window(x0, y0, x1, y1);
    st7789_write_cmd(0x2C);                              /* Memory Write */
}

void st7789_write_pixels(const uint16_t *pixels, uint32_t count)
{
    if (!pixels || count == 0) return;

    gpio_direction_output(PIN_CS, 0);
    gpio_direction_output(PIN_DC, 1);

    const uint8_t *bytes = (const uint8_t *)pixels;
    uint32_t byte_count = count * 2;

    JL_SPI1->BAUD = ST7789_SPI_BAUD;

    /* DMA requires 4-byte aligned address and word-multiple count.
     * If unaligned or small payload, use direct register byte writes. */
    if (byte_count <= 4 || (((uint32_t)bytes) & 0x3) != 0) {
        JL_SPI1->CON |= 0x4000;          /* Clear pending */
        for (uint32_t i = 0; i < byte_count; i++) {
            JL_SPI1->BUF = bytes[i];
            while ((JL_SPI1->CON & 0x8000) == 0);
            JL_SPI1->CON |= 0x4000;
        }
    } else {
        /* Hardware DMA transfer: transfer in 480-byte chunks */
        __asm__ volatile("ssync; csync" ::: "memory");
        while (byte_count > 0) {
            uint32_t chunk = (byte_count > 480) ? 480 : byte_count;
            JL_SPI1->CON |= 0x4000;          /* Clear pending */
            JL_SPI1->ADR = (uint32_t)bytes;
            JL_SPI1->CNT = chunk;

            while ((JL_SPI1->CON & 0x8000) == 0);
            JL_SPI1->CON |= 0x4000;

            bytes += chunk;
            byte_count -= chunk;
        }
    }

    gpio_direction_output(PIN_CS, 1);
}

void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (w == 0 || h == 0) return;
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

    /* 1. Send Window Coordinates (CAS/RAS) followed by RAMWR (0x2C) with CS toggles */
    st7789_prepare_window(x, y, x + w - 1, y + h - 1);

    /* 2. Assert CS and DC for continuous RAMWR data streaming */
    gpio_direction_output(PIN_CS, 0);
    gpio_direction_output(PIN_DC, 0);   /* DC = 0 (Command) */

    JL_SPI1->CON |= 0x4000;
    JL_SPI1->BUF = 0x2C;            /* 0x2C Memory Write */
    while ((JL_SPI1->CON & 0x8000) == 0);
    JL_SPI1->CON |= 0x4000;

    gpio_direction_output(PIN_DC, 1);   /* DC = 1 (Data streaming) */

    uint32_t total_pixels = (uint32_t)w * h;
    uint8_t byte_hi = (uint8_t)(color & 0xFF);
    uint8_t byte_lo = (uint8_t)(color >> 8);

    if (total_pixels < 240) {
        /* Small batches: write directly to SPI buffer register to avoid buffer copy overhead */
        JL_SPI1->BAUD = ST7789_SPI_BAUD;
        for (uint32_t i = 0; i < total_pixels; i++) {
            JL_SPI1->BUF = byte_hi;
            while ((JL_SPI1->CON & 0x8000) == 0);
            JL_SPI1->CON |= 0x4000;

            JL_SPI1->BUF = byte_lo;
            while ((JL_SPI1->CON & 0x8000) == 0);
            JL_SPI1->CON |= 0x4000;
        }
    } else {
        /* Large rectangles (full screen fills):
         * Use DMA in 240-pixel (480-byte, 32-bit word aligned) chunks. */
        #define CHUNK_SIZE 240
        static uint16_t line_buf[CHUNK_SIZE] __attribute__((aligned(32)));
        for (uint32_t i = 0; i < CHUNK_SIZE; i++) {
            line_buf[i] = color;
        }
        __asm__ volatile("ssync; csync" ::: "memory");

        while (total_pixels > 0) {
            uint32_t batch = (total_pixels > CHUNK_SIZE) ? CHUNK_SIZE : total_pixels;
            JL_SPI1->BAUD = ST7789_SPI_BAUD;
            JL_SPI1->CON |= 0x4000;         /* Clear pending */
            JL_SPI1->ADR = (uint32_t)line_buf;
            JL_SPI1->CNT = batch * 2;

            while ((JL_SPI1->CON & 0x8000) == 0);
            JL_SPI1->CON |= 0x4000;
            total_pixels -= batch;
        }
    }

    gpio_direction_output(PIN_CS, 1);    /* CS = 1 (Concludes RAMWR and resets SPI framing) */
}

void st7789_fill_screen(uint16_t color)
{
    st7789_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

void st7789_flush_framebuffer(const uint16_t *framebuffer)
{
    if (!framebuffer) return;

    /* 1. Set full 240x240 screen bounding box */
    st7789_prepare_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

    /* 2. Issue RAMWR (0x2C) command */
    gpio_direction_output(PIN_CS, 0);
    gpio_direction_output(PIN_DC, 0);

    JL_SPI1->CON |= 0x4000;
    JL_SPI1->BUF = 0x2C;
    while ((JL_SPI1->CON & 0x8000) == 0);
    JL_SPI1->CON |= 0x4000;

    gpio_direction_output(PIN_DC, 1);

    /* 3. Stream entire 115,200 bytes using hardware DMA in 240-pixel (480-byte) chunks
     * to prevent FIFO starving and maintain exact 32-bit DMA alignment constraints. */
    const uint8_t *ptr = (const uint8_t *)framebuffer;
    uint32_t bytes_remaining = LCD_WIDTH * LCD_HEIGHT * 2;
    const uint32_t chunk_bytes = LCD_WIDTH * 2; /* 480 bytes per row */

    JL_SPI1->BAUD = ST7789_SPI_BAUD;
    __asm__ volatile("ssync; csync" ::: "memory");

    while (bytes_remaining > 0) {
        JL_SPI1->CON |= 0x4000;
        JL_SPI1->ADR = (uint32_t)ptr;
        JL_SPI1->CNT = chunk_bytes;
        bytes_remaining -= chunk_bytes;

        while ((JL_SPI1->CON & 0x8000) == 0);
        JL_SPI1->CON |= 0x4000;
        ptr += chunk_bytes;
    }

    gpio_direction_output(PIN_CS, 1);    /* CS = 1 (Concludes RAMWR and resets SPI framing) */
}

void st7789_backlight(uint8_t enable)
{
    gpio_direction_output(PIN_BL, enable ? 1 : 0);
}

void st7789_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    int16_t dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int16_t dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;

    while (1) {
        if (x0 >= 0 && x0 < LCD_WIDTH && y0 >= 0 && y0 < LCD_HEIGHT) {
            st7789_set_window((uint16_t)x0, (uint16_t)y0, (uint16_t)x0, (uint16_t)y0);
            st7789_write_pixels(&color, 1);
        }
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void st7789_init(void)
{
    /* 1. Configure GPIO pins as outputs using SDK gpio driver. Keep CS INACTIVE (HIGH) during power-up and reset! */
    gpio_direction_output(PIN_CS, 1);   /* CS = 1 (High/Inactive) */
    gpio_direction_output(PIN_DC, 0);   /* DC = 0 */
    gpio_direction_output(PIN_RST, 1);  /* RST = 1 */
    gpio_direction_output(PIN_BL, 0);   /* BL = 0 */
    gpio_set_die(PIN_CS, 1);
    gpio_set_die(PIN_DC, 1);
    gpio_set_die(PIN_RST, 1);
    gpio_set_die(PIN_BL, 1);

    /* 2. Configure Pin Crossbar for SPI1 controller (clear bit 4, ROM @0x020251FC) */
    JL_IOMAP->CON1 &= ~0x10;

    /* 3. Initialize hardware SPI1 peripheral */
    JL_SPI1->CON = 0x4021;
    JL_SPI1->BAUD = ST7789_SPI_BAUD;

    /* 4. Power-on rail stabilization delay (100 ms before touching RESX) */
    delay_ms(100);

    /* 5. Hardware Reset Pulse (15 ms low, 20 ms high before first command) */
    gpio_direction_output(PIN_RST, 0); /* Assert Reset (low) */
    delay_ms(15);
    gpio_direction_output(PIN_RST, 1); /* De-assert Reset (high) */
    delay_ms(20);

    /* 6. Software Reset (0x01 SWRESET) followed by full 150 ms state machine reload */
    st7789_write_cmd(0x01);
    delay_ms(150);

    /* 7. Sleep Out (0x11 SLPOUT) followed by strict 150 ms charge pump stabilization */
    st7789_write_cmd(0x11);
    delay_ms(150);

    /* 8. Send Configuration, Color Mode, and Calibrated Gamma tables */
    const uint8_t madctl[] = { 0x00 };
    st7789_write_cmd_params(0x36, madctl, sizeof(madctl));

    const uint8_t colmod[] = { 0x05 }; /* 16-bit RGB565 (matching ROM 0x02058c0c) */
    st7789_write_cmd_params(0x3A, colmod, sizeof(colmod));

    const uint8_t porch[] = { 0x0C, 0x0C, 0x00, 0x33, 0x33 };
    st7789_write_cmd_params(0xB2, porch, sizeof(porch));

    const uint8_t gate[] = { 0x35 };
    st7789_write_cmd_params(0xB7, gate, sizeof(gate));

    const uint8_t vcom[] = { 0x32 };
    st7789_write_cmd_params(0xBB, vcom, sizeof(vcom));

    const uint8_t vdv_vrh_en[] = { 0x01 };
    st7789_write_cmd_params(0xC2, vdv_vrh_en, sizeof(vdv_vrh_en));

    const uint8_t vrh[] = { 0x15 };
    st7789_write_cmd_params(0xC3, vrh, sizeof(vrh));

    const uint8_t vdv[] = { 0x20 };
    st7789_write_cmd_params(0xC4, vdv, sizeof(vdv));

    const uint8_t frctrl[] = { 0x0F }; /* 60 Hz */
    st7789_write_cmd_params(0xC6, frctrl, sizeof(frctrl));

    const uint8_t pwr1[] = { 0xA4, 0xA1 };
    st7789_write_cmd_params(0xD0, pwr1, sizeof(pwr1));

    const uint8_t pv_gamma[] = {
        0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31, 0x33,
        0x48, 0x17, 0x14, 0x15, 0x31, 0x34
    };
    st7789_write_cmd_params(0xE0, pv_gamma, sizeof(pv_gamma));

    const uint8_t nv_gamma[] = {
        0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31, 0x33,
        0x48, 0x17, 0x14, 0x15, 0x31, 0x34
    };
    st7789_write_cmd_params(0xE1, nv_gamma, sizeof(nv_gamma));

    st7789_write_cmd(0x21); /* Display Inversion ON */
    delay_ms(10);

    /* 9. Display ON (0x29 DISPON) followed by 50 ms settling before drawing */
    st7789_write_cmd(0x29);
    delay_ms(50);

    /* Keep backlight OFF while initial UI buffers are rendered into ST7789 GRAM */
    st7789_backlight(0);
}
