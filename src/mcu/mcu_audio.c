/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_audio.c
 * SMK-37 Pro Audio Output Driver - External I2S (CS4344) via JL_ALNK0
 *
 * Hardware chain: SoC ALNK0 I2S master on Port C -> CS4344 external DAC
 * (sitting next to the headphone jack) -> onboard amp -> jack.
 *
 *   - PC0 = I2S MCLK,   PC1 = I2S SCLK (bit clock)
 *   - PC2 = I2S LRCLK (frame sync), PC6 = ALNK0 Ch3 I2S SDOUT (Pad 38)
 *   - JL_IOMAP->CON5[7:6] = 00 routes ALNK0 to Port C
 *   - JL_CLOCK->CLK_CON2[8] = 1 enables the ALNK0 peripheral clock
 *   - JL_ANA->PLL_CON1[25] = 1 selects the 48 kHz audio PLL
 *   - JL_ANA->PLL_CON1[29] = 1 energizes the analog audio PLL clock domain (set at boot)
 *
 * ARCHITECTURAL RATIONALE: Direct Register Access vs. Vendor SDK
 * -------------------------------------------------------------
 * 1. Ping-Pong DMA Streaming in the Hot Path:
 *    - To maintain low-latency, glitch-free audio, the driver connects directly
 *      to ALNK0 IRQ 11 (alnk_isr).
 *    - Synthesizer PCM is rendered directly into double-buffered DMA SRAM slots
 *      (audio_dma_buf) with zero intermediate memory copies.
 *    - The vendor SDK iis_open() forces dynamic heap allocation (malloc) for
 *      DMA buffers and introduces callback indirection. We use deterministic,
 *      32-byte cache-aligned static SRAM.
 *
 * 2. Board-Level Pin Control via SDK:
 *    - Port C pin configuration (PC0..PC6) uses standard SDK GPIO functions
 *      (gpio_direction_output, gpio_set_die, gpio_set_hd, gpio_set_hd1).
 *    - PC6 is ALNK0 Channel 3 I2S SDOUT (Pad 38). It is configured once at boot
 *      and driven exclusively by the ALNK0 peripheral. Never toggle PC6 as GPIO.
 */

#include "asm/cpu.h"
#include "asm/hwi.h"
#include "FreeRTOS.h"
#include "task.h"

#include "hal_audio.h"
#include "hal_midi.h"
#include "mcu_audio.h"
#include "driver/device/gpio.h"

#include <stdint.h>
#include <string.h>


#define AUDIO_SAMPLE_RATE       48000            /* I2S frame rate */
#define AUDIO_BLOCK_FRAMES      128              /* Frames per DMA sub-buffer (~2.67ms @ 48kHz) */
#define AUDIO_DMA_SUB_WORDS     (AUDIO_BLOCK_FRAMES * 2)   /* 256 words = 1024 bytes */
#define AUDIO_DMA_TOTAL_WORDS   (AUDIO_DMA_SUB_WORDS * 2)  /* 512 words = 2048 bytes */

/* Static cache-line aligned double-buffered DMA ring */
static int32_t audio_dma_buf[AUDIO_DMA_TOTAL_WORDS] __attribute__((aligned(32)));
static HalAudioCallback audio_cb = NULL;
static volatile uint32_t audio_block_count = 1;

/* Core 1 Real-time Audio Render Synchronization Flags */
volatile uint8_t audio_render_ready = 0;
volatile uint8_t audio_render_subbuf = 0;

extern void request_irq(unsigned char index, unsigned char priority, void (*handler)(void), unsigned char cpu_id);

/* Configure Port C audio pins using vendor SDK GPIO functions.
 * PC0 = I2S MCLK, PC1 = I2S SCLK, PC2 = I2S LRCLK, PC6 = ALNK0 Channel 3 SDOUT (to CS4344 SDIN).
 * All pins configured for high drive, digital enable, and handed over to ALNK0. */
static void config_audio_gpio(void)
{
    static const unsigned int AUDIO_PINS[] = {
        IO_PORTC_00, IO_PORTC_01, IO_PORTC_02, IO_PORTC_06
    };
    for (size_t i = 0; i < sizeof(AUDIO_PINS) / sizeof(AUDIO_PINS[0]); i++) {
        unsigned int p = AUDIO_PINS[i];
        gpio_direction_output(p, 0);
        gpio_set_die(p, 1);
        gpio_set_hd(p, 1);
        gpio_set_hd1(p, 1);
        gpio_set_pull_up(p, 0);
        gpio_set_pull_down(p, 0);
    }
}

int32_t *mcu_audio_get_subbuf_ptr(uint8_t subbuf_idx)
{
    return &audio_dma_buf[(subbuf_idx & 1) * AUDIO_DMA_SUB_WORDS];
}

void mcu_audio_render_quantum(int32_t *dma_dst, const int16_t *pcm16, size_t frames)
{
    if (!dma_dst || frames == 0) return;

    if (!pcm16) {
        for (size_t i = 0; i < frames * 2; i++) {
            dma_dst[i] = 0;
        }
        return;
    }

    /* Shift left by 8 bits (<< 8) for standard 24-bit I2S transmission
     * to the CS4344 DAC in 32-bit slot mode. Master volume scaling has
     * already been applied strictly before the soft limiter in synth_engine. */
    for (size_t i = 0; i < frames * 2; i++) {
        dma_dst[i] = (int32_t)pcm16[i] << 8;
    }
}

void mcu_audio_flush_subbuf(uint8_t subbuf_idx)
{
    (void)subbuf_idx;
    /* Internal SRAM is uncached; drain store buffer so DMA reads fresh samples */
    __asm__ volatile("ssync; csync" ::: "memory");
}

__attribute__((interrupt(""))) static void alnk_isr(void)
{
    uint8_t con2 = JL_ALNK0->CON2;
    if (con2 & 0xF0) {
        /* Acknowledge pending DMA interrupt by clearing all channel pending flags */
        JL_ALNK0->CON2 = 0x0F;
        audio_block_count++;

        /* For Channel 3 (CON1 = 0x5000), bit 15 of CON0 indicates which sub-buffer
         * hardware DMA is currently reading:
         *   If bit 15 is 1 -> DMA reading Sub-buffer 1 -> render into Sub-buffer 0 (idx 0).
         *   If bit 15 is 0 -> DMA reading Sub-buffer 0 -> render into Sub-buffer 1 (idx 1).
         */
        uint8_t write_idx = (JL_ALNK0->CON0 & (1 << 15)) ? 0 : 1;
        audio_render_subbuf = write_idx;
        audio_render_ready = 1;
    }
    __asm__ volatile("ssync; csync" ::: "memory");
}

void mcu_audio_render_block(uint8_t subbuf_idx, const int16_t *pcm16, size_t frames)
{
    int32_t *dst = mcu_audio_get_subbuf_ptr(subbuf_idx);
    mcu_audio_render_quantum(dst, pcm16, frames);
    mcu_audio_flush_subbuf(subbuf_idx);
    if (hal_usb_audio_is_active()) {
        hal_usb_audio_push_samples(pcm16, frames);
    }
}

static inline void alnk_start(void)
{
    __asm__ volatile("ssync; csync" ::: "memory"); /* vendor iis_open CoreSynchronize() */
    JL_ALNK0->CON0 |= (1 << 11);      /* bit11: DMA run/start */
}

static inline void alnk_stop(void)
{
    JL_ALNK0->CON0 &= ~(1 << 11);
    __asm__ volatile("ssync; csync" ::: "memory");
}

/* =========================================================================
 * HAL Audio Driver Interface
 * ========================================================================= */
int hal_audio_init(uint32_t sample_rate, HalAudioCallback callback)
{
    (void)sample_rate;
    audio_cb = callback;

    /* 1. Configure Port C pads: MCLK(PC0), SCLK(PC1), LRCLK(PC2), DATA0(PC3), AMP(PC6)
     *    using vendor SDK gpio driver with high drive and digital enable. */
    config_audio_gpio();

    /* 2. Pre-fill initial silence into both DMA sub-buffers */
    memset(audio_dma_buf, 0, sizeof(audio_dma_buf));
    __asm__ volatile("ssync; csync" ::: "memory");

    /* 3. Enable ALNK0 peripheral clocks (48 kHz audio PLL):
     *    Stock ROM @ 0x0200724C first clears CLK_CON2[11:8], then enables the
     *    48 kHz PLL (ANA_PLL_CON1 bit 25), energizes domain power gate (bit 29),
     *    waits for VCO lock, and selects the 48 kHz PLL in CLK_CON2 (bit 8 = 1, bit 9 = 0). */
    JL_CLOCK->CLK_CON2 &= ~0x00000F00;
    JL_ANA->PLL_CON1   |= 0x02000000; /* 48 kHz PLL enable (bit 25) */
    JL_ANA->PLL_CON1   |= 0x20000000; /* Clock domain power gate (bit 29) */

    /* Allow analog audio PLL oscillator to stabilize and lock (~100 us) */
    for (volatile int i = 0; i < 30000; i++) {
        __asm__ volatile("nop");
    }

    JL_CLOCK->CLK_CON2 |= (1 << 8);   /* Select 48 kHz audio PLL */
    JL_CLOCK->CLK_CON2 &= ~(1 << 9);  /* PLL_ALNK_SEL = 0 */

    /* 4. Route ALNK0 to Port C (clear IOMAP CON5 bits 7:6). */
    JL_IOMAP->CON5 &= ~(3 << 6);

    /* 5. Reset ALNK0 block (stock ROM @ 0x020071A4). */
    JL_ALNK0->CON0 = 0;
    JL_ALNK0->CON1 = 0;
    JL_ALNK0->CON2 = 0x0F;
    JL_ALNK0->CON3 = 0;

    /* 6. Clean pipeline configuration:
     *    CON3 = 0x83: Master 256fs ratio (ROM @ 0x020071F0 / 0x02007264)
     *    CON0 = 0x0180: FALL edge (bit10=0), 64fs mode (bit9=0), MCLK out (bit8=1), SCLK out (bit7=1)
     *                   Stock ROM @ 0x02007240 explicitly clears bit 9. In 64fs mode, SCLK runs at
     *                   32 clocks/channel, matching 32-bit slot width in CON1 and CS4344 256x mode.
     *    CON1 = 0x5000: Channel 3 on PC6 enabled in 32-bit slot mode (bit 12 = enable, bit 14 = 32-bit mode)
     *    CON2 = 0x0F: Clear pending flags */
    JL_ALNK0->CON3 = 0x83;
    JL_ALNK0->CON0 = (1 << 8) | (1 << 7);
    JL_ALNK0->CON1 = 0x5000;
    JL_ALNK0->CON2 = 0x0F;

    audio_block_count = 1;
    audio_render_ready = 0;
    audio_render_subbuf = 0;

    /* 7. Point DMA address registers to double-buffered DMA ring */
    JL_ALNK0->ADR0 = (uint32_t)audio_dma_buf;
    JL_ALNK0->ADR1 = (uint32_t)audio_dma_buf;
    JL_ALNK0->ADR2 = (uint32_t)audio_dma_buf;
    JL_ALNK0->ADR3 = (uint32_t)audio_dma_buf;
    JL_ALNK0->LEN  = AUDIO_DMA_SUB_WORDS; /* 256 points (128 stereo frames) per sub-buffer */

    /* 8. Register ALNK0 interrupt (IRQ 11) to Core 1 (cpu_id = 1) */
    request_irq(IRQ_ALNK_IDX, 3, alnk_isr, 1);

    return 0;
}

void hal_audio_start(void)
{
    JL_ALNK0->CON2 = 0x0F;
    alnk_start();

    /* Allow I2S clocks & CS4344 to lock (~100,000 cycles, ROM @ 0x0200731C) */
    for (volatile int i = 0; i < 100000; i++) {
        __asm__ volatile("nop");
    }
}

void hal_audio_stop(void)
{
    alnk_stop();
}

void hal_audio_set_volume(uint8_t volume)
{
    (void)volume;
}

uint32_t hal_audio_get_block_count(void)
{
    return audio_block_count;
}