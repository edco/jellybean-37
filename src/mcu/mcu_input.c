/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_input.c
 * SMK-37 Pro Matrix Scanner & Velocity Capture (Autonomous SPI2 DMA + Soft2 ISR)
 * Velocity = Scan-frame transit time evaluated against stock 5-point piecewise curve.
 */

#include "asm/cpu.h"
#include "asm/hwi.h"
#include "FreeRTOS.h"
#include "task.h"
#include "os/os_api.h"

#include "hal_input.h"
#include "hal_time.h"
#include "driver/device/gpio.h"
#include "st7789.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "asm/p33.h"

#ifndef AT_RAM_CODE
#define AT_RAM_CODE          SEC_USED(.volatile_ram_code)
#endif
#ifndef AT_RAM_DATA
#define AT_RAM_DATA          SEC_USED(.volatile_ram)
#endif

#define NUM_KEYS              37
#define MATRIX_ROWS           16
#define MATRIX_COLS           8


/* Derived peripheral clock and SPI2 baud rate configuration for key matrix scanning:
 * Row scan target period is ~9.6 us per row (~104 kHz row cadence, SPI_CLK ~ 3.333 MHz).
 * BAUD = round(LSB_CLK / (2 * SPI_CLK)) - 1
 * At 320 MHz: HSB = 160 MHz, LSB = 40 MHz -> round(40 / 6.666) - 1 = 6 - 1 = 5
 * At 360 MHz: HSB = 180 MHz, LSB = 45 MHz -> round(45 / 6.666) - 1 = 7 - 1 = 6 (SPI ~ 3.214 MHz)
 */
#if defined(CPU_FREQ_MHZ) && (CPU_FREQ_MHZ == 360)
#define MCU_LSB_CLK_HZ        45000000UL
#else
#define MCU_LSB_CLK_HZ        40000000UL
#endif

#ifndef KEYBED_SCAN_FREQ_HZ
#define KEYBED_SCAN_FREQ_HZ   2000UL
#endif

/* 16 rows * 32 clock cycles per row = 512 SPI clocks per full matrix frame.
 * Target SPI clock = KEYBED_SCAN_FREQ_HZ * 16 rows * 64 clock periods for reliable settle.
 * Base frequency: 2000 Hz -> 3.333 MHz. */
#define SPI2_TARGET_CLK_HZ    ((KEYBED_SCAN_FREQ_HZ * 512UL > 3333333UL) ? (KEYBED_SCAN_FREQ_HZ * 512UL) : 3333333UL)
#define SPI2_BAUD_DIVISOR     (((MCU_LSB_CLK_HZ + SPI2_TARGET_CLK_HZ) / (2UL * SPI2_TARGET_CLK_HZ)) - 1UL)



/* JL_ADC: lsfr_base + map_adr(0x31,0) => CON @ 0x13100, RES @ 0x13104.
 * Sampled per-phase non-blocking from the SPI2 wrap ISR. */


#define ADC_PHASE_MASK        0x0F


extern void request_irq(unsigned char index, unsigned char priority, void (*handler)(void), unsigned char cpu_id);
extern void unrequest_irq(unsigned char index, unsigned char cpu_id);
extern int clk_get(const char *name);

static void evaluate_keybed(const volatile uint8_t *frame);
AT_RAM_CODE __attribute__((interrupt(""))) static void soft2_matrix_isr(void);
AT_RAM_CODE __attribute__((interrupt(""))) static void saradc_analog_isr(void);



/* 16-row active-low walking bit shift register output table (exact stock ROM @ 0x020585E8) */
static AT_RAM_DATA uint32_t row_shift_patterns[MATRIX_ROWS] __attribute__((aligned(32))) = {
    0xFEFFFFFF, 0xFDFFFFFF, 0xFBFFFFFF, 0xF7FFFFFF,
    0xEFFFFFFF, 0xDFFFFFFF, 0xBFFFFFFF, 0x7FFFFFFF,
    0xFFFEFFFF, 0xFFFDFFFF, 0xFFFBFFFF, 0xFFF7FFFF,
    0xFFEFFFFF, 0xFFDFFFFF, 0xFFBFFFFF, 0xFF7FFFFF
};

/* Exact stock ROM front-panel button lamp mapping @ 0x02057CBC
 * Upper nibble: Bank (0 or 1), Lower nibble: bit index in 74HC595 shift word (0..14).
 * Bits 3..5 are reserved for 74HC4051 analog mux channel code and are never touched by lamps. */
static const uint8_t lamp_table[19] = {
    0x0C, /* Lamp  0: Knob Bank    (Bank 0, bit 12) */
    0x12, /* Lamp  1: Fader Bank   (Bank 1, bit  2) */
    0x00, /* Lamp  2: Nav Left     (Bank 0, bit  0) */
    0x10, /* Lamp  3: Nav Right    (Bank 1, bit  0) */
    0x0D, /* Lamp  4: Octave Down  (Bank 0, bit 13) */
    0x1D, /* Lamp  5: Octave Up    (Bank 1, bit 13) */
    0x02, /* Lamp  6: Arp          (Bank 0, bit  2) */
    0x01, /* Lamp  7: Note Repeat  (Bank 0, bit  1) */
    0x09, /* Lamp  8: Scale        (Bank 0, bit  9) */
    0x08, /* Lamp  9: Chord        (Bank 0, bit  8) */
    0x0B, /* Lamp 10: Globe        (Bank 0, bit 11) */
    0x0A, /* Lamp 11: BT           (Bank 0, bit 10) */
    0x11, /* Lamp 12: Patch        (Bank 1, bit  1) */
    0x19, /* Lamp 13: Para         (Bank 1, bit  9) */
    0x18, /* Lamp 14: FX           (Bank 1, bit  8) */
    0x1A, /* Lamp 15: Seq          (Bank 1, bit 10) */
    0x1C, /* Lamp 16: Seq Play G   (Bank 1, bit 12) */
    0x1E, /* Lamp 17: Seq Play R   (Bank 1, bit 14) */
    0x1B, /* Lamp 18: Seq Rec R    (Bank 1, bit 11) */
};

static volatile uint32_t lamp_mask = 0;
static volatile uint16_t bank_words[2] = { 0xFFFF, 0xFFFF };
static volatile uint16_t active_lamp_bits = 0xFFFF;
static volatile uint8_t  lamp_bank_index = 0;
static volatile uint8_t  lamp_bank_pass = 0;

static void update_lamp_bank_words(void)
{
    uint16_t b0 = 0xFFFF;
    uint16_t b1 = 0xFFFF;

    for (uint8_t i = 0; i < 19; i++) {
        if (lamp_mask & (1u << i)) {
            uint8_t entry = lamp_table[i];
            uint8_t bank = (entry >> 4) & 1;
            uint8_t bit = entry & 0x0F;
            if (bank == 0) {
                b0 &= ~(1u << bit);
            } else {
                b1 &= ~(1u << bit);
            }
        }
    }
    bank_words[0] = b0;
    bank_words[1] = b1;
}

void mcu_input_set_lamp(uint8_t lamp_idx, bool on)
{
    if (lamp_idx >= 19) return;
    if (on) {
        lamp_mask |= (1u << lamp_idx);
    } else {
        lamp_mask &= ~(1u << lamp_idx);
    }
    update_lamp_bank_words();
}

bool mcu_input_get_lamp(uint8_t lamp_idx)
{
    if (lamp_idx >= 19) return false;
    return (lamp_mask & (1u << lamp_idx)) != 0;
}

void mcu_input_set_lamp_mask(uint32_t mask)
{
    lamp_mask = mask & 0x7FFFF;
    update_lamp_bank_words();
}

uint32_t mcu_input_get_lamp_mask(void)
{
    return lamp_mask;
}


/* Exact stock ROM 37-key matrix contact coordinate table @ 0x02058738 */
static const uint8_t key_contact_table[NUM_KEYS][2] = {
    { 0x10, 0x52 }, { 0x20, 0x72 }, { 0x30, 0x92 }, { 0x40, 0xB2 },
    { 0x50, 0xD2 }, { 0x60, 0xF2 }, { 0x70, 0xE2 }, { 0x80, 0xC2 },
    { 0x90, 0xA2 }, { 0xA0, 0x82 }, { 0xC0, 0x62 }, { 0xD0, 0x42 },
    { 0xE0, 0x22 }, { 0xF0, 0x12 }, { 0xB0, 0x32 }, { 0x11, 0x33 },
    { 0x21, 0x43 }, { 0x31, 0x53 }, { 0x41, 0x63 }, { 0x51, 0x73 },
    { 0x61, 0x83 }, { 0x71, 0x93 }, { 0x81, 0xA3 }, { 0x91, 0xB3 },
    { 0xA1, 0xC3 }, { 0xB1, 0xD3 }, { 0xC1, 0xE3 }, { 0xD1, 0x23 },
    { 0xE1, 0x13 }, { 0xF1, 0x14 }, { 0x64, 0x24 }, { 0x74, 0x34 },
    { 0x84, 0x44 }, { 0x94, 0x54 }, { 0xA4, 0xD4 }, { 0xB4, 0xF4 },
    { 0xC4, 0xE4 }
};

typedef enum {
    KEY_ST_RELEASED = 0,
    KEY_ST_DEPRESSING,
    KEY_ST_PRESSED,
    KEY_ST_RELEASING
} KeyTrackState;

#define KEY_PRESS_LOCKOUT_FRAMES     8   /* ~4.0 ms lockout after make */
#define KEY_RELEASE_LOCKOUT_FRAMES   12  /* ~6.0 ms lockout after break */
#define KEY_RELEASED_SETTLE_FRAMES   4   /* ~2.0 ms settle after C1 opens */

typedef struct {
    KeyTrackState state;
    uint8_t       debounce_frames;
    uint8_t       last_velocity;
    uint16_t      transit_frames;
} KeyTracking;

/* Sub-scans per 10 ms RTOS tick in POLLED mode, and the gap between them.
 *
 * These two together set the VELOCITY QUANTISATION STEP, because
 * evaluate_keybed() counts the c1->c2 transit once per sub-scan -- which is
 * what the stock firmware does too (the ROM increments its per-key counter
 * once per completed 16-phase matrix frame). The stock firmware's physical
 * transit windows are ~1.2 ms (hard), 1.8-2.2 ms (medium) and 10-25 ms (soft)
 * (docs/DISASSEMBLY_NOTES.md:441-448). Hard and medium are only ~800 us apart,
 * so a sub-scan period of ~900 us (what 10 sub-frames x 700 us spacing gave)
 * quantised both into the SAME bucket -- which is why only ~3 distinct
 * velocities were observable, and why soft always read soft (it sits 12x
 * further away and never collides).
 *
 * The old spacing was dead time that bought nothing but that coarseness: the
 * scan work itself is only ~200 us per sub-frame (16 rows x 32 bits of 10-nop
 * shifting, plus the 2 us latch and 3 us settle per row). Dropping the
 * spacing to 0 and raising the count to 30 samples the matrix every ~200 us,
 * so a 1.2 ms transit spans ~6 samples and a 2.0 ms transit ~10 -- separable
 * again.
 *
 * Unlike the previous hal_get_us() timestamps, the frame counter is not
 * limited by hal_get_us()'s 500 us granularity, which was coarse enough to
 * collapse hard against medium on its own.
 *
 * CPU budget: 30 x ~200 us = ~6 ms of the 10 ms tick, which is LESS total
 * scan work than the previous 10 x ~900 us = ~9 ms. The GUI therefore gains
 * roughly 3 ms per tick rather than losing any, while velocity resolution
 * improves by ~4.5x. */
static KeyTracking key_track[NUM_KEYS];
static AT_RAM_DATA volatile uint8_t matrix_raw[2][MATRIX_ROWS] __attribute__((aligned(32)));

/* ISR-side frame publishing: ISR fills buffer fill_buf, publishes index into
 * pub_buf; consumers read only matrix_raw[pub_buf] (never the back buffer). */
static volatile uint8_t fill_buf = 0;
static volatile uint8_t pub_buf = 0;

/* Frame-ready: the SPI2 wrap ISR publishes a frame by flipping the double
 * buffer and bumping scan_frame_cnt; vKeybedScanTask polls that counter.
 * (The ISR must NOT call os_sem_post — documented unsafe from an interrupt
 * function, and the self-bisect showed the fault appears exactly when the
 * frame-ready post is enabled. The vendor FreeRTOS lib also omits task
 * notifications, so the consumer polls instead.) */

/* =========================================================================
 * Self-bisecting ISR-stage diagnostic (temporary, 2026-09-29).
 *
 * Symptom: with the 2 kHz SPI2 DMA ISR armed, the MCU deterministically
 * faults inside lv_txt_get_size() during ui_sample_init(); the SAME code with
 * SCAN_MODE_POLLED (as on `main`) boots fine. No OOB write, no cache/heap
 * overlap, valid font chain, correct ISR prologue were found statically.
 *
 * isr_stage gates the ISR body feature-by-feature so ONE flash can identify
 * the exact operation that corrupts the concurrently-running GUI task. The GUI
 * task draws isr_stage as 4 bits on row 5 cols 0..3 (Green=1, Grey=0), then
 * stresses lv_txt_get_size(). Whichever pattern is on screen when the system
 * freezes is the culprit stage.
 *   0x00 re-arm SPI2 only (2 kHz preemption, nothing else)
 *   0x01 + 595 latch / read_columns_fast / store matrix_raw
 *   0x02 + flush_dcache(s_spi2_dma_buf, 32)   (spins on cache status bit 14)
 *   0x04 + SAR-ADC kick/read on wrap
 *   0x08  + (was os_sem_post; now a no-op -- consumer polls the counter)
 * ========================================================================= */
#define ISR_ST_MATRIX  0x01
#define ISR_ST_FLUSH   0x02
#define ISR_ST_ADC     0x04
#define ISR_ST_SEM     0x08

volatile uint8_t isr_stage = ISR_ST_MATRIX | ISR_ST_ADC;
static volatile uint8_t  scan_step = 0;
static volatile uint32_t scan_frame_cnt = 0;

#ifndef AD_CH_PA08
#define AD_CH_PA08 0x1
#endif
#ifndef AD_CH_PA10
#define AD_CH_PA10 0x2
#endif
#ifndef AD_CH_PC09
#define AD_CH_PC09 0x8
#endif

#define TOTAL_ANALOG_PHASES    19
#define PHASE_PITCH_WHEEL      16
#define PHASE_MOD_WHEEL        17
#define PHASE_FADER_WRAP       18

/* Stock drum pad scan permutation @ 0x02057B40 */
static const uint8_t pad_permutation[SMK_NUM_PADS] = {
    1, 2, 3, 0, 4, 7, 5, 6, 9, 10, 11, 8, 12, 15, 13, 14
};

/* Stock fader sub-sweep channel map @ 0x0205743B */
static const uint8_t fader_channel_map[SMK_NUM_FADERS] = {
    1, 2, 3, 0
};

/* Analog scan phase tracking (stock 3-step SPI2/SAR-ADC hardware handshake) */
static volatile uint8_t  analog_phase = 0;
static volatile uint8_t  fader_sub_slot = 0;
static volatile uint8_t  current_adc_ch = AD_CH_PA08;
static volatile uint8_t  scan_interlock_state = 0;

static volatile uint16_t adc_pad_val[SMK_NUM_PADS] = {0};
static volatile uint32_t adc_pad_gen[SMK_NUM_PADS] = {0};

static volatile uint16_t adc_wheel_val[2] = {0};
static volatile uint32_t adc_wheel_gen[2] = {0};

static volatile uint16_t adc_fader_val[SMK_NUM_FADERS] = {0};
static volatile uint32_t adc_fader_gen[SMK_NUM_FADERS] = {0};

/* Pad strike detector constants (~2.13ms per pad sample rate).
 *
 * Noise rejection rests on three things:
 *  1. a strike must be corroborated by PAD_STRIKE_CONFIRM separate samples
 *     above the threshold (one ADC glitch is not a note),
 *  2. PAD_MIN_PEAK is an EXCESS over baseline, not an absolute reading,
 *  3. the release threshold sits below the arming threshold, so a reading
 *     sitting on the boundary cannot chatter.
 */
#define PAD_BASELINE_CEIL      0x0C0  /* adaptive baseline climb ceiling (192) */
#define PAD_STRIKE_DELTA       12     /* baseline + delta arms the detector */
#define PAD_RELEASE_DELTA      4      /* baseline + delta RELEASES; must be < PAD_STRIKE_DELTA
                                       * to give an 8-count hysteresis band. Without it a pad
                                       * hovering on the threshold could arm, serve out the
                                       * settle count, release, and immediately re-arm. */
#define PAD_STRIKE_CONFIRM     2      /* samples above threshold required before dispatching a
                                       * note. 1 = old behaviour, which fired on a single
                                       * glitch. Pads are struck rather than switched, so a real
                                       * impact stays above threshold for several ms and 2 costs
                                       * nothing; it also detects FASTER than before, because the
                                       * old code had to fill a 3-scan window first. */
#define PAD_STRIKE_SINGLE_OK   0      /* if non-zero, one sample with at least this much excess
                                       * over baseline is trusted on its own. Escape hatch for
                                       * very fast light taps if PAD_STRIKE_CONFIRM=2 ever
                                       * feels too stiff; 0 = disabled. */
#define PAD_MIN_PEAK           20     /* minimum excess over baseline to report a strike */
#define PAD_RELEASE_SETTLE     8      /* quiet scans before release dispatch (~17ms at 2.1ms/sweep) */
#define PAD_RETRIG_COOLDOWN    6      /* scans of strike lockout after release (~13ms at 2.1ms/sweep) */

/* Pad strike velocity scaling:
 * raw peak <= 192 -> velocity 1; >= 831 -> velocity 127 */
#define PAD_PEAK_MIN           192
#define PAD_PEAK_MAX           703
#define PAD_PEAK_SPAN          (PAD_PEAK_MAX - PAD_PEAK_MIN) /* 639 */

/* Pad aftertouch scaling:
 * held raw <= 832 -> aftertouch 0; >= 859 -> aftertouch 127 */
#define PAD_AT_MIN             832
#define PAD_AT_MAX             959
#define PAD_AT_SPAN            (PAD_AT_MAX - PAD_AT_MIN)     /* 27 */

/* Fader/wheel scaling (10-bit ADC -> controller domain) */
#define ADC_PITCH_HALF_SPAN     105   /* Physical pitch wheel travel from center to either stop (~105 counts) */
#define ADC_WHEEL_DEADZONE      16    /* Quiet band around physical center */
#define ADC_MOD_TOP             405   /* Mod wheel physical top stop (full modulation = 127) */
#define ADC_MOD_BOTTOM          630   /* Mod wheel physical bottom stop (resting = 0) */
#define ADC_MOD_SPAN            (ADC_MOD_BOTTOM - ADC_MOD_TOP) /* ~225 counts */
#define FADER_DEADBAND          2     /* Scalar counts of change to report */



static inline uint8_t read_columns_fast(void)
{
    uint32_t porta = JL_PORTA->IN;
    uint32_t porth = JL_PORTH->IN;

    return (uint8_t)(
        ((porta & (1 << 2)) >> 2)       |
        ((porta >> 4) & 0x0E)           |
        ((porta >> 5) & 0x10)           |
        ((porth >> 1) & 0x20)           |
        ((porth >> 2) & 0xC0)
    );
}

/* =========================================================================
 * Non-blocking SAR-ADC kick (atomic single-write matching stock 0x0201BDB4).
 * ========================================================================= */
static inline void adc_kick_channel(uint8_t ch)
{
    JL_ADC->CON = ((uint32_t)(ch & 0x0F) << 8) | 0x407D;
}

/* =========================================================================
 * SPI2 DMA Completion ISR (~32 us cadence ~= 2 kHz frame rate)
 * Directly replicates stock ROM ProcessPadMatrixCaptureIsr @ 0x0201BDB4.
 * ========================================================================= */
AT_RAM_CODE __attribute__((interrupt(""))) static void spi2_dma_matrix_isr(void)
{
    uint8_t st = isr_stage;

    /* Stock 3-step SAR-ADC interlock (ProcessPadMatrixCaptureIsr @ 0x0201BDB4):
     * State 1: ADC ISR staged new mux code into shift pattern.
     * State 2: SPI2 shifted out the row pattern with new mux code.
     * State 3: PA1 latch pulsed LOW, 74HC4051 mux physically selected and settled. Kick ADC!
     * State 4+: Conversion in progress; SAR-ADC hardware IRQ 24 will fire when ready. */
    if ((st & ISR_ST_ADC) && scan_interlock_state > 0) {
        if (scan_interlock_state == 3) {
            adc_kick_channel(current_adc_ch);
        }
        if (scan_interlock_state < 32) {
            scan_interlock_state++;
        } else {
            /* Watchdog: if conversion is dropped or delayed, recover */
            scan_interlock_state = 1;
        }
    }

    /* Clear SPI2 interrupt pending flag (Bit 14 W1C) */
    JL_SPI2->CON |= (1 << 14);
    __asm__ volatile ("ssync; csync" ::: "memory");

    /* Sample columns settled during the previous row transfer (matches stock 0x0201BDB4) */
    if (st & ISR_ST_MATRIX) {
        uint8_t cols = read_columns_fast();
        matrix_raw[fill_buf][scan_step] = cols;
    }

    /* Drive latch pin PA1 HIGH to begin pre-latch */
    JL_PORTA->OUT |= (1 << 1);

    /* Advance row step */
    uint8_t next_step = (scan_step + 1) & 0x0F;

    /* Publish completed frame on wrap to row 0 and raise Soft IRQ 2 */
    if (next_step == 0) {
        scan_frame_cnt++;

        /* Publish the completed fill frame to consumers */
        pub_buf = fill_buf;
        fill_buf ^= 1;

        /* Trigger Soft IRQ 2 for keybed, encoder, and pad evaluation */
        q32DSP(0)->ILAT_SET = 0x4;
    }

    scan_step = next_step;

    /* Re-arm SPI2 DMA for NEXT row directly from precomputed pattern table */
    JL_SPI2->ADR = (uint32_t)&row_shift_patterns[scan_step];
    JL_SPI2->CNT = 4;

    /* Drive latch pin PA1 LOW (falling edge latches outputs) */
    JL_PORTA->OUT &= ~(1 << 1);

    __asm__ volatile ("ssync" ::: "memory");
    __asm__ volatile ("csync" ::: "memory");
}

/* =========================================================================
 * SAR-ADC Conversion ISR (IRQ 24 / 0x18, Priority 3)
 * Directly replicates stock ROM AdvancePadMatrixScanPhaseIsr @ 0x0201D76C.
 * Advances the 19-phase analog pipeline every ~112 us (~2.13 ms full sweep).
 * ========================================================================= */
AT_RAM_CODE __attribute__((interrupt(""))) static void saradc_analog_isr(void)
{
    /* Clear ADC pending bit (write-1-to-clear bit 6, matching stock 0x0201D778) */
    JL_ADC->CON |= (1 << 6);
    __asm__ volatile ("ssync; csync" ::: "memory");

    uint16_t res = (uint16_t)(JL_ADC->RES & 0x3FF);

    /* Store converted sample for completed phase */
    if (analog_phase < SMK_NUM_PADS) {
        uint8_t pad_idx = pad_permutation[analog_phase];
        adc_pad_val[pad_idx] = res;
        adc_pad_gen[pad_idx]++;
    } else if (analog_phase == PHASE_PITCH_WHEEL) {
        adc_wheel_val[0] = res;
        adc_wheel_gen[0]++;
    } else if (analog_phase == PHASE_MOD_WHEEL) {
        adc_wheel_val[1] = res;
        adc_wheel_gen[1]++;
    } else if (analog_phase == PHASE_FADER_WRAP) {
        uint8_t fader_idx = fader_channel_map[fader_sub_slot];
        adc_fader_val[fader_idx] = res;
        adc_fader_gen[fader_idx]++;
        fader_sub_slot = (fader_sub_slot + 1) & 3;
    }

    /* Advance to next analog phase (0..18) */
    analog_phase++;
    if (analog_phase >= TOTAL_ANALOG_PHASES) {
        analog_phase = 0;
    }

    /* Determine next mux code and ADC channel */
    uint8_t mux_code;
    if (analog_phase < 8) {
        mux_code = analog_phase;
        current_adc_ch = AD_CH_PA08;
    } else if (analog_phase < 16) {
        mux_code = analog_phase & 7;
        current_adc_ch = AD_CH_PC09;
    } else if (analog_phase == PHASE_PITCH_WHEEL) {
        mux_code = 4;
        current_adc_ch = AD_CH_PA10;
    } else if (analog_phase == PHASE_MOD_WHEEL) {
        mux_code = 5;
        current_adc_ch = AD_CH_PA10;
    } else {
        mux_code = fader_sub_slot;
        current_adc_ch = AD_CH_PA10;
    }

    /* Stage next mux address code into Byte 0 and merge with active lamp bank bits */
    uint32_t lamp_low16 = (active_lamp_bits & 0xFF07) | 0x00C0 | ((uint32_t)(mux_code & 7) << 3);
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        row_shift_patterns[r] = (row_shift_patterns[r] & 0xFFFF0000) | lamp_low16;
    }

    /* Re-arm interlock: state 1 releases SPI2 ISR to shift pattern, latch, and kick at state 3 */
    scan_interlock_state = 1;

    __asm__ volatile ("ssync" ::: "memory");
    __asm__ volatile ("csync" ::: "memory");
}

/* =========================================================================
 * Keybed Contact Tracking (Break/Make Evaluation in Task Context)
 * ========================================================================= */
/* Stock frame cadence. The ROM increments the transit counter once per
 * completed 16-phase matrix frame (~26 us each), and the 5-point curve's
 * breakpoints are denominated in stock frames. */
#define STOCK_FRAME_PERIOD_US       26u

/* Stock 5-point velocity curve, shared by all 37 keys.
 *
 * Source: the factory default seeded by the ROM config loader at
 * 0x02005126, which resets r3 = r4+1162 INSIDE the per-key loop body, so the
 * same 10 bytes are replicated into all 37 slots. There is no per-key
 * calibration. A populated mnt/sdfile/app/usrtrim overrides this: the ROM
 * mallocs the file (0x01C33434), validates an 8-byte header, and copies 548
 * bytes to 0x01C35CF0 (+0x2A90); the curve slots begin at offset +168,
 * i.e. 0x01C35D98.
 *
 * The 16 pressure/aftertouch pads at +0x2A98 share a SECOND curve,
 * (50, 550, 720, 850, 950), seeded the same way from r4+1152. */
static const uint16_t k_stock_key_curve[5] = { 5, 640, 800, 920, 980 };

/* Stock 5-point piecewise remap (ROM 0x0201C1F0..0x0201C26A).
 *
 * `transit` is the per-key c1->c2 scan-frame count: the ROM seeds it to 10 on
 * contact-1 close, increments it once per completed 16-phase matrix frame
 * while the key is depressing, and latches it when the deep contact closes.
 *
 * The ROM uses one array for both axes, remapping the input thresholds onto a
 * uniform 0/256/512/768/1023 output axis. That output is 1..1023; callers
 * here want the 1..127 MIDI domain, so it is scaled at the end. */
static uint8_t compute_key_velocity(uint16_t transit)
{
    uint16_t x = (transit > 1022) ? 1u : (uint16_t)(1023u - transit);

    uint8_t seg = 0;
    while (seg < 5 && x > k_stock_key_curve[seg]) {
        seg++;
    }

    uint32_t out;
    if (seg == 0) {
        out = 1;
    } else if (seg == 5) {
        out = 1023;
    } else {
        uint16_t lo = k_stock_key_curve[seg - 1];
        uint16_t hi = k_stock_key_curve[seg];
        int32_t  num = ((int32_t)(x - lo)) << 8;
        int32_t  den = (int32_t)hi - (int32_t)lo;
        out = (256u * (uint32_t)(seg - 1)) + (uint32_t)(num / den);
        if (out == 0) {
            out = 1;
        }
    }

    /* The ROM's interpolating segments can overshoot 1023 (e.g. x=998 lands at
     * 1100). Clamp before scaling into 1..127. */
    if (out > 1023) {
        out = 1023;
    }
    return (uint8_t)(1u + ((out * 126u) / 1023u));
}

#define KEY_EVT_RING_SIZE 64
typedef struct {
    uint8_t key_index;
    bool pressed;
    uint8_t velocity;
} IsrKeyEvt;

static volatile IsrKeyEvt key_evt_ring[KEY_EVT_RING_SIZE];
static volatile uint8_t key_evt_head = 0;
static volatile uint8_t key_evt_tail = 0;

static inline void post_key_event(uint8_t key_index, bool pressed, uint8_t velocity)
{
    uint8_t next = (key_evt_head + 1) % KEY_EVT_RING_SIZE;
    if (next != key_evt_tail) {
        key_evt_ring[key_evt_head].key_index = key_index;
        key_evt_ring[key_evt_head].pressed = pressed;
        key_evt_ring[key_evt_head].velocity = velocity;
        key_evt_head = next;
    }
}

static void drain_key_events(void)
{
    while (key_evt_tail != key_evt_head) {
        IsrKeyEvt evt = key_evt_ring[key_evt_tail];
        key_evt_tail = (key_evt_tail + 1) % KEY_EVT_RING_SIZE;
        hal_input_dispatch_key(evt.key_index, evt.pressed, evt.velocity);
    }
}

static void evaluate_keybed(const volatile uint8_t *frame)
{
    for (uint8_t k = 0; k < NUM_KEYS; k++) {
        uint8_t c1_coord = key_contact_table[k][0];
        uint8_t c2_coord = key_contact_table[k][1];

        uint8_t c1_row = (c1_coord >> 4) & 0x0F;
        uint8_t c1_col = c1_coord & 0x07;
        uint8_t c2_row = (c2_coord >> 4) & 0x0F;
        uint8_t c2_col = c2_coord & 0x07;

        bool c1_open = ((frame[c1_row] & (1 << c1_col)) != 0);
        bool c2_closed = ((frame[c2_row] & (1 << c2_col)) == 0);

        KeyTracking *kt = &key_track[k];

        /* Stock transit counter (ROM 0x0201C0E0): bumped once per scan frame
         * while the key is mid-press, BEFORE the deep contact is evaluated,
         * which is the order the ROM uses. Each frame is ~26-50 us -> increment by 1 frame. */
        if (kt->state == KEY_ST_DEPRESSING) {
            if (kt->transit_frames < 1023) {
                kt->transit_frames++;
            }
        }

        switch (kt->state) {
        case KEY_ST_RELEASED:
            if (kt->debounce_frames > 0) {
                kt->debounce_frames--;
                break;
            }
            /* Test the DEEP contact first, exactly as main does. A c2 closure
             * is a strike no matter which state we were in, so a press whose
             * contact-1 window happened to fall between two samples still
             * fires instead of stranding the key. */
            if (c2_closed) {
                kt->debounce_frames = KEY_PRESS_LOCKOUT_FRAMES;
                /* Contact 1 was never seen (both closed within one sample):
                 * fall back to the stock seed so it reads as a hard strike. */
                uint16_t transit = kt->transit_frames ? kt->transit_frames : 10;
                uint8_t vel = compute_key_velocity(transit);
                kt->last_velocity = vel;
                kt->state = KEY_ST_PRESSED;
                kt->transit_frames = 0;
                post_key_event(k, true, vel);
            } else if (!c1_open) {
                /* Contact 1 closed, deep contact not yet: seed the stock
                 * transit counter and open the strike window. */
                kt->transit_frames = 10;
                kt->state = KEY_ST_DEPRESSING;
            }
            break;

        case KEY_ST_DEPRESSING:
            if (c2_closed) {
                kt->debounce_frames = KEY_PRESS_LOCKOUT_FRAMES;
                /* Same mapping as the RELEASED arm; the counter was already
                 * bumped for this frame above. */
                uint16_t transit = kt->transit_frames ? kt->transit_frames : 10;
                uint8_t vel = compute_key_velocity(transit);
                kt->last_velocity = vel;
                kt->state = KEY_ST_PRESSED;
                kt->transit_frames = 0;
                post_key_event(k, true, vel);
            } else if (c1_open) {
                /* Contact 1 re-opened without the deep contact ever closing:
                 * an aborted/aborted-bounce press, back to idle. */
                kt->state = KEY_ST_RELEASED;
                kt->debounce_frames = KEY_RELEASED_SETTLE_FRAMES;
                kt->transit_frames = 0;
            }
            break;

        case KEY_ST_PRESSED:
            if (kt->debounce_frames > 0) {
                kt->debounce_frames--;
                break;
            }
            if (!c2_closed) {
                kt->state = KEY_ST_RELEASING;
                kt->debounce_frames = KEY_RELEASE_LOCKOUT_FRAMES;
                kt->transit_frames = 0;
                post_key_event(k, false, 0);
            }
            break;

        case KEY_ST_RELEASING:
            if (kt->debounce_frames > 0) {
                kt->debounce_frames--;
                break;
            }
            /* Wait for Contact 1 to fully open before re-arming the key
             * for a subsequent strike. This guarantees liftoff bounce or chatter
             * on C2 cannot trigger a phantom hard note-on. */
            if (c1_open) {
                kt->state = KEY_ST_RELEASED;
                kt->debounce_frames = KEY_RELEASED_SETTLE_FRAMES;
            }
            break;
        }
    }
}

/* =========================================================================
 * Rotary Encoders K1..K8 (Exact stock ROM contact codes @ 0x02057B10)
 * All ride on Column 7 (Bit 7 / 0x80)
 * ========================================================================= */
typedef struct {
    uint8_t row_a;
    uint8_t row_b;
} EncoderMapping;

static const EncoderMapping encoder_map[8] = {
    { 10,  9 }, /* K1: 0xA7, 0x97 */
    {  4,  3 }, /* K2: 0x47, 0x37 */
    {  2,  1 }, /* K3: 0x27, 0x17 */
    { 12, 11 }, /* K4: 0xC7, 0xB7 */
    {  6,  5 }, /* K5: 0x67, 0x57 */
    {  8,  7 }, /* K6: 0x87, 0x77 */
    { 14, 13 }, /* K7: 0xE7, 0xD7 */
    {  0, 15 }  /* K8: 0x07, 0xF7 */
};

static const int8_t quad_lut[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

static uint8_t encoder_prev_state[8] = {0};
static int8_t  encoder_accum[8] = {0};

typedef struct {
    uint8_t enc_index;
    int8_t  delta;
} IsrEncEvt;

#define ENC_EVT_RING_SIZE 32
static volatile IsrEncEvt enc_evt_ring[ENC_EVT_RING_SIZE];
static volatile uint8_t enc_evt_head = 0;
static volatile uint8_t enc_evt_tail = 0;

static inline void post_encoder_event(uint8_t enc_index, int8_t delta)
{
    uint8_t next = (enc_evt_head + 1) % ENC_EVT_RING_SIZE;
    if (next != enc_evt_tail) {
        enc_evt_ring[enc_evt_head].enc_index = enc_index;
        enc_evt_ring[enc_evt_head].delta = delta;
        enc_evt_head = next;
    }
}

static void drain_encoder_events(void)
{
    while (enc_evt_tail != enc_evt_head) {
        IsrEncEvt evt = enc_evt_ring[enc_evt_tail];
        enc_evt_tail = (enc_evt_tail + 1) % ENC_EVT_RING_SIZE;
        hal_input_dispatch_encoder(evt.enc_index, evt.delta);
    }
}

static void process_encoders(const volatile uint8_t *frame)
{
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t a = ((frame[encoder_map[i].row_a] & 0x80) == 0) ? 1 : 0;
        uint8_t b = ((frame[encoder_map[i].row_b] & 0x80) == 0) ? 1 : 0;
        uint8_t curr = (a << 1) | b;
        uint8_t prev = encoder_prev_state[i];

        if (curr != prev) {
            int8_t step = quad_lut[(prev << 2) | curr];
            encoder_accum[i] += step;
            encoder_prev_state[i] = curr;

            if (curr == 0) {
                if (encoder_accum[i] >= 2) {
                    post_encoder_event(i, 1);
                } else if (encoder_accum[i] <= -2) {
                    post_encoder_event(i, -1);
                }
                encoder_accum[i] = 0;
            } else if (encoder_accum[i] >= 4) {
                post_encoder_event(i, 1);
                encoder_accum[i] = 0;
            } else if (encoder_accum[i] <= -4) {
                post_encoder_event(i, -1);
                encoder_accum[i] = 0;
            }
        }
    }
}

/* =========================================================================
 * Drum Pads (16 channels evaluated in Soft IRQ 2 at ~2 kHz scan frame rate)
 * ========================================================================= */
typedef enum {
    PAD_EVT_NOTE,
    PAD_EVT_PRESSURE,
    PAD_EVT_STRIKE_DEBUG,
    PAD_EVT_AT_DEBUG,
} PadEvtType;

typedef struct {
    uint8_t        pad_index;
    PadEvtType     type;
    bool           pressed;
    uint8_t        velocity;
    uint8_t        pressure;
    uint16_t       raw;
    uint16_t       baseline;
    uint16_t       samples[4];
    uint8_t        sample_cnt;
} IsrPadEvt;

#define PAD_EVT_RING_SIZE 64
static volatile IsrPadEvt pad_evt_ring[PAD_EVT_RING_SIZE];
static volatile uint8_t pad_evt_head = 0;
static volatile uint8_t pad_evt_tail = 0;

static inline void post_pad_event(uint8_t pad_index, bool pressed, uint8_t velocity)
{
    uint8_t next = (pad_evt_head + 1) % PAD_EVT_RING_SIZE;
    if (next != pad_evt_tail) {
        pad_evt_ring[pad_evt_head].pad_index = pad_index;
        pad_evt_ring[pad_evt_head].type = PAD_EVT_NOTE;
        pad_evt_ring[pad_evt_head].pressed = pressed;
        pad_evt_ring[pad_evt_head].velocity = velocity;
        pad_evt_head = next;
    }
}

static inline void post_pad_pressure_event(uint8_t pad_index, uint8_t pressure)
{
    uint8_t next = (pad_evt_head + 1) % PAD_EVT_RING_SIZE;
    if (next != pad_evt_tail) {
        pad_evt_ring[pad_evt_head].pad_index = pad_index;
        pad_evt_ring[pad_evt_head].type = PAD_EVT_PRESSURE;
        pad_evt_ring[pad_evt_head].pressure = pressure;
        pad_evt_head = next;
    }
}

static inline void post_pad_strike_event(uint8_t pad_index, uint16_t baseline, const uint16_t *samples, uint8_t count, uint16_t peak, uint8_t velocity)
{
    post_pad_event(pad_index, true, velocity);

    uint8_t next = (pad_evt_head + 1) % PAD_EVT_RING_SIZE;
    if (next != pad_evt_tail) {
        pad_evt_ring[pad_evt_head].pad_index = pad_index;
        pad_evt_ring[pad_evt_head].type = PAD_EVT_STRIKE_DEBUG;
        pad_evt_ring[pad_evt_head].baseline = baseline;
        pad_evt_ring[pad_evt_head].raw = peak;
        pad_evt_ring[pad_evt_head].velocity = velocity;
        uint8_t n = (count > 4) ? 4 : count;
        pad_evt_ring[pad_evt_head].sample_cnt = n;
        for (uint8_t i = 0; i < n; i++) {
            pad_evt_ring[pad_evt_head].samples[i] = samples[i];
        }
        pad_evt_head = next;
    }
}

static inline void post_pad_at_debug_event(uint8_t pad_index, uint16_t raw, uint16_t avg, uint8_t pressure)
{
    uint8_t next = (pad_evt_head + 1) % PAD_EVT_RING_SIZE;
    if (next != pad_evt_tail) {
        pad_evt_ring[pad_evt_head].pad_index = pad_index;
        pad_evt_ring[pad_evt_head].type = PAD_EVT_AT_DEBUG;
        pad_evt_ring[pad_evt_head].raw = raw;
        pad_evt_ring[pad_evt_head].baseline = avg;
        pad_evt_ring[pad_evt_head].pressure = pressure;
        pad_evt_head = next;
    }
}

static void drain_pad_events(void)
{
    while (pad_evt_tail != pad_evt_head) {
        IsrPadEvt evt = pad_evt_ring[pad_evt_tail];
        pad_evt_tail = (pad_evt_tail + 1) % PAD_EVT_RING_SIZE;
        switch (evt.type) {
        case PAD_EVT_NOTE:
            hal_input_dispatch_pad(evt.pad_index, evt.pressed, evt.velocity);
            break;
        case PAD_EVT_PRESSURE:
            hal_input_dispatch_pad_pressure(evt.pad_index, evt.pressure);
            break;
        case PAD_EVT_STRIKE_DEBUG:
            printf("[PAD %u STRIKE] base:%u raw:[", (unsigned int)evt.pad_index, (unsigned int)evt.baseline);
            for (uint8_t i = 0; i < evt.sample_cnt; i++) {
                printf("%u%s", (unsigned int)evt.samples[i], (i + 1 < evt.sample_cnt) ? ", " : "");
            }
            printf("] peak:%u vel:%u\n", (unsigned int)evt.raw, (unsigned int)evt.velocity);
            break;
        case PAD_EVT_AT_DEBUG:
            printf("[PAD %u AT] raw:%u (avg:%u press:%u)\n",
                   (unsigned int)evt.pad_index, (unsigned int)evt.raw,
                   (unsigned int)evt.baseline, (unsigned int)evt.pressure);
            break;
        }
    }
}

typedef struct {
    uint16_t baseline;         /* adaptive noise floor */
    uint16_t peak;             /* peak ADC seen during a pending strike */
    uint8_t  pressed;          /* pad currently held */
    uint8_t  settle_count;     /* consecutive quiet scans while held */
    uint8_t  cooldown;         /* post-release retrigger lockout (sweeps) */
    uint8_t  strike_hits;      /* samples seen above threshold since the strike began;
                                * nonzero also means "a strike is pending", which is what
                                * suppresses baseline adaptation */
    uint32_t last_gen;         /* gen of last processed sample */

    /* Strike telemetry */
    uint16_t strike_samples[4];
    uint8_t  strike_sample_cnt;
    uint32_t last_at_log_ms;

    /* Continuous pressure / aftertouch */
    uint16_t running_average;
    uint8_t  last_pressure;
} PadSlotState;

static PadSlotState pad_slot_state[SMK_NUM_PADS] = {{0}};

static uint8_t pad_velocity_from_peak(uint16_t peak)
{
    if (peak <= PAD_PEAK_MIN) {
        return 1;
    }
    if (peak >= PAD_PEAK_MAX) {
        return 127;
    }
    uint32_t frac = (uint32_t)(peak - PAD_PEAK_MIN) * 126;
    uint32_t v = 1 + (frac / PAD_PEAK_SPAN);
    if (v > 127) v = 127;
    return (uint8_t)v;
}

/* Commit a pending strike: mark the pad held and publish the note-on.
 * Shared by both entry paths so they cannot drift apart. */
static void pad_dispatch_strike(uint8_t p, PadSlotState *st, uint16_t val)
{
    st->pressed = 1;
    st->settle_count = 0;
    st->running_average = val;
    st->last_pressure = 0;
    st->last_at_log_ms = hal_get_ms();
    uint8_t vel = pad_velocity_from_peak(st->peak);
    post_pad_strike_event(p, st->baseline, st->strike_samples, st->strike_sample_cnt, st->peak, vel);
    st->peak = 0;
    st->strike_hits = 0;
    st->strike_sample_cnt = 0;
}

static void evaluate_pads(void)
{
    for (uint8_t p = 0; p < SMK_NUM_PADS; p++) {
        uint32_t gen = adc_pad_gen[p];
        PadSlotState *st = &pad_slot_state[p];
        if (gen == st->last_gen) continue;
        st->last_gen = gen;

        uint16_t val = adc_pad_val[p];

        /* Adaptive baseline noise floor (decay -1, creep +2 to ceiling) */
        if (!st->pressed && st->strike_hits == 0) {
            if ((int)val < (int)st->baseline) {
                if (st->baseline) st->baseline--;
            } else if (st->baseline < PAD_BASELINE_CEIL) {
                st->baseline += 2;
            }
        }
        if (st->cooldown) st->cooldown--;

        uint16_t strike_thresh = st->baseline + PAD_STRIKE_DELTA;
        /* Hysteresis: releasing needs the reading to fall further than arming did. */
        uint16_t release_thresh = st->baseline + PAD_RELEASE_DELTA;

        if (!st->pressed) {
            /* Pad open: strike detection, dispatched as soon as it is corroborated.
             *
             * The previous code armed on one above-threshold sample and then had a
             * "quick tap" path that dispatched on the very next scan if the reading
             * fell back -- so a single ADC glitch produced a note. Its
             * PAD_MIN_PEAK guard did not help, because it compared the ABSOLUTE
             * reading (always >= ~112 here) against 20 and so was always true.
             * Cross-talk from a large chord makes those glitches likelier, which is
             * why the phantom notes clustered on big chords.
             *
             * Now a strike needs PAD_STRIKE_CONFIRM separate samples above the
             * threshold, or one sample carrying an excess that noise cannot
             * plausibly explain. Dispatch is immediate on confirmation, so this is
             * FASTER than the old fixed 3-scan window, not slower. */
            if (val > strike_thresh && st->cooldown == 0) {
                if (val > st->peak) st->peak = val;
                if (st->strike_hits < 0xFF) st->strike_hits++;

                if (st->strike_sample_cnt < 4) {
                    st->strike_samples[st->strike_sample_cnt++] = val;
                }

                /* Peak measured as an excess over baseline, so the gate means
                 * something regardless of where the pad's resting level sits. */
                uint16_t excess = (st->peak > st->baseline)
                                ? (uint16_t)(st->peak - st->baseline) : 0;
                int corroborated = (st->strike_hits >= PAD_STRIKE_CONFIRM)
                                || (PAD_STRIKE_SINGLE_OK > 0 &&
                                    st->strike_hits >= 1 &&
                                    excess >= PAD_STRIKE_SINGLE_OK);

                if (corroborated && excess >= PAD_MIN_PEAK) {
                    pad_dispatch_strike(p, st, val);
                }
            } else if (st->strike_hits != 0) {
                /* Fell back below the threshold before corroboration: this was a
                 * glitch, not a strike. Drop it silently. */
                st->peak = 0;
                st->strike_hits = 0;
                st->strike_sample_cnt = 0;
            }
        } else {
            /* Pad held: check release and track continuous aftertouch */
            if (val <= release_thresh) {
                if (++st->settle_count >= PAD_RELEASE_SETTLE) {
                    st->pressed = 0;
                    st->peak = 0;
                    st->strike_hits = 0;
                    st->cooldown = PAD_RETRIG_COOLDOWN;
                    st->last_pressure = 0;
                    st->running_average = 0;
                    st->strike_sample_cnt = 0;
                    post_pad_event(p, false, 0);
                    post_pad_pressure_event(p, 0);
                }
            } else {
                st->settle_count = 0;

                /* Smooth running pressure (75% prev + 25% new) */
                st->running_average = (uint16_t)(((uint32_t)st->running_average * 3 + val) >> 2);

                /* Scaled aftertouch: <= 832 -> 0, >= 859 -> 127 */
                uint16_t at_input = st->running_average;
                uint8_t pressure;
                if (at_input <= PAD_AT_MIN) {
                    pressure = 0;
                } else if (at_input >= PAD_AT_MAX) {
                    pressure = 127;
                } else {
                    pressure = (uint8_t)(((uint32_t)(at_input - PAD_AT_MIN) * 127) / PAD_AT_SPAN);
                }

                if (pressure != st->last_pressure) {
                    st->last_pressure = pressure;
                    post_pad_pressure_event(p, pressure);
                }

                /* Periodic aftertouch console reporting every ~250ms */
                uint32_t now = hal_get_ms();
                if (now - st->last_at_log_ms >= 250) {
                    st->last_at_log_ms = now;
                    post_pad_at_debug_event(p, val, st->running_average, pressure);
                }
            }
        }
    }
}

/* =========================================================================
 * SOFT2 Software Interrupt Service Routine (IRQ 122 / 0x7A, Priority 4)
 * Raised by spi2_dma_matrix_isr on every completed 16-phase scan frame.
 * Directly replicates stock ROM ProcessPadKeysAndLampsSoftIsr @ 0x0201C03A.
 * ========================================================================= */
AT_RAM_CODE __attribute__((interrupt(""))) static void soft2_matrix_isr(void)
{
    __asm__ volatile ("csync" ::: "memory");
    q32DSP(0)->ILAT_CLR = 0x4;
    __asm__ volatile ("ssync; csync" ::: "memory");

    /* Evaluate 37-key velocity and edge state on the freshly published frame */
    evaluate_keybed(matrix_raw[pub_buf]);

    /* Evaluate 8 rotary encoders at full frame rate (~76 us), matching stock ROM */
    process_encoders(matrix_raw[pub_buf]);

    /* Evaluate 16 velocity pads at 2 kHz scan frame rate */
    evaluate_pads();

    /* Lamp bank multiplexing (exact stock ROM @ 0x0201C36E) */
    lamp_bank_pass++;
    if (lamp_bank_pass >= 48) {
        lamp_bank_pass = 0;
        lamp_bank_index ^= 1;
        /* Blank both banks during switchover (active-low bank disables) */
        HUSB_PHY0_CON0 = (HUSB_PHY0_CON0 | 0x1000F) | (1 << 17) | (1 << 18);
        active_lamp_bits = 0xFFFF;
    } else {
        active_lamp_bits = bank_words[lamp_bank_index];
        if (lamp_bank_index == 0) {
            /* Enable Bank 0 (bit 17 low, bit 18 high) */
            HUSB_PHY0_CON0 = ((HUSB_PHY0_CON0 | 0x1000F) | (1 << 18)) & ~(1 << 17);
        } else {
            /* Enable Bank 1 (bit 18 low, bit 17 high) */
            HUSB_PHY0_CON0 = ((HUSB_PHY0_CON0 | 0x1000F) | (1 << 17)) & ~(1 << 18);
        }
    }

    /* Apply active lamp pattern to all 16 rows, preserving analog mux code in bits 3..5 */
    uint32_t lamp_code = (active_lamp_bits & 0xFF07) | 0x00C0;
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        row_shift_patterns[r] = (row_shift_patterns[r] & 0xFFFF0038) | lamp_code;
    }

    __asm__ volatile ("ssync" ::: "memory");
    __asm__ volatile ("csync" ::: "memory");
}

/* =========================================================================
 * Panel Pushbuttons
 * ========================================================================= */
typedef struct {
    uint8_t row;
    uint8_t col;
} ButtonMapping;

/* Exact stock ROM front-panel button contact table @ 0x02057C85 */
AT_RAM_CODE static const ButtonMapping button_map[SMK_BTN_COUNT] = {
    [SMK_BTN_KNOB_BANK]   = { 2, 6 }, /* Ch  0: 0x26 */
    [SMK_BTN_FADER_BANK]  = { 7, 6 }, /* Ch  1: 0x76 */
    [SMK_BTN_NAV_LEFT]    = { 9, 6 }, /* Ch  2: 0x96 */
    [SMK_BTN_NAV_RIGHT]   = { 5, 6 }, /* Ch  3: 0x56 */
    [SMK_BTN_OCTAVE_DOWN] = { 6, 6 }, /* Ch  4: 0x66 */
    [SMK_BTN_OCTAVE_UP]   = { 3, 6 }, /* Ch  5: 0x36 */
    [SMK_BTN_ARP]         = { 7, 5 }, /* Ch  6: 0x75 */
    [SMK_BTN_NOTE_REPEAT] = { 6, 5 }, /* Ch  7: 0x65 */
    [SMK_BTN_SCALE]       = { 2, 5 }, /* Ch  8: 0x25 */
    [SMK_BTN_CHORD]       = { 4, 5 }, /* Ch  9: 0x45 */
    [SMK_BTN_GLOBE]       = { 1, 5 }, /* Ch 10: 0x15 */
    [SMK_BTN_BT]          = { 8, 6 }, /* Ch 11: 0x86 */
    [SMK_BTN_PATCH]       = { 9, 5 }, /* Ch 12: 0x95 */
    [SMK_BTN_PARA]        = { 5, 5 }, /* Ch 13: 0x55 */
    [SMK_BTN_FX]          = { 8, 5 }, /* Ch 14: 0x85 */
    [SMK_BTN_SEQ]         = { 3, 5 }, /* Ch 15: 0x35 */
    [SMK_BTN_SEQ_PLAY]    = { 1, 6 }, /* Ch 16: 0x16 */
    [SMK_BTN_SEQ_REC]     = { 4, 6 }, /* Ch 17: 0x46 */
};

#define BTN_DEBOUNCE_SAMPLES 3

typedef struct {
    uint8_t raw;        /* last raw sample */
    uint8_t count;      /* consecutive equal samples */
    uint8_t last_disp;  /* last dispatched state */
} ButtonDebounce;

static ButtonDebounce button_debounce[SMK_BTN_COUNT] = {{0}};
static bool any_btn_active = false;

AT_RAM_CODE static void process_buttons(const volatile uint8_t *frame)
{
    /* Fast path: all 18 buttons reside on Columns 5 and 6 (bits 5 & 6, mask 0x60)
     * across Rows 1..9. Matrix inputs are active-low (0 = pressed, 1 = open).
     * If no button row has bits 5 or 6 low and no debounce is pending, return immediately. */
    uint8_t active_mask = 0;
    for (uint8_t r = 1; r <= 9; r++) {
        active_mask |= ((frame[r] & 0x60) ^ 0x60);
    }
    if (active_mask == 0 && !any_btn_active) {
        return;
    }

    bool still_active = false;
    for (uint8_t b = 0; b < SMK_BTN_COUNT; b++) {
        uint8_t row = button_map[b].row;
        uint8_t col = button_map[b].col;
        bool pressed = ((frame[row] & (1 << col)) == 0);
        uint8_t raw = pressed ? 1 : 0;
        ButtonDebounce *db = &button_debounce[b];

        if (raw != db->raw) {
            db->raw = raw;
            db->count = 0;
        } else if (db->count < BTN_DEBOUNCE_SAMPLES) {
            db->count++;
        }
        if (db->count >= BTN_DEBOUNCE_SAMPLES && db->last_disp != raw) {
            db->last_disp = raw;
            hal_input_dispatch_button((SmkButton)b, raw != 0);
        }
        if (raw || db->last_disp || db->count < BTN_DEBOUNCE_SAMPLES) {
            still_active = true;
        }
    }
    any_btn_active = still_active;
}

/* =========================================================================
 * Analog controls (wheels / faders). Pads are evaluated in Soft IRQ 2.
 * ========================================================================= */
static uint8_t  fader_published[SMK_NUM_FADERS] = {0};
static uint32_t fader_last_gen[SMK_NUM_FADERS] = {0};
static uint32_t fader_accum[SMK_NUM_FADERS] = {0};
static uint8_t  fader_count[SMK_NUM_FADERS] = {0};
static bool     fader_first[SMK_NUM_FADERS] = { true, true, true, true };

static int16_t  wheel_val[2] = {0, 0};
static uint32_t wheel_last_gen[2] = {0, 0};
static bool     wheel_first[2] = { true, true };
static uint16_t pitch_center = 516;

static void process_wheel_controls(void)
{
    /* Pitch wheel (bipolar: -8191..+8191, spring-centered) */
    if (adc_wheel_gen[0] != wheel_last_gen[0]) {
        wheel_last_gen[0] = adc_wheel_gen[0];
        uint16_t val = adc_wheel_val[0];
        if (wheel_first[0]) {
            wheel_first[0] = false;
            if (val >= 450 && val <= 580) {
                pitch_center = val;
            }
            wheel_val[0] = 0;
        } else {
            /* Pushing forward bends up (+), pulling back bends down (-) */
            int32_t delta = (int32_t)val - (int32_t)pitch_center;
            int16_t v;
            if (delta > -ADC_WHEEL_DEADZONE && delta < ADC_WHEEL_DEADZONE) {
                v = 0;
            } else {
                int32_t scaled = (delta * 8191) / ADC_PITCH_HALF_SPAN;
                if (scaled > 8191) scaled = 8191;
                if (scaled < -8191) scaled = -8191;
                v = (int16_t)scaled;
            }
            if (v != wheel_val[0]) {
                wheel_val[0] = v;
                hal_input_dispatch_wheel(SMK_WHEEL_PITCH, v);
            }
        }
    }

    /* Mod wheel (unipolar: 0..127, bottom=0, top=127) */
    if (adc_wheel_gen[1] != wheel_last_gen[1]) {
        wheel_last_gen[1] = adc_wheel_gen[1];
        uint16_t val = adc_wheel_val[1];
        /* Bottom position is high ADC (~630), Top position is low ADC (~405) */
        int32_t scaled = ((int32_t)ADC_MOD_BOTTOM - (int32_t)val) * 127 / ADC_MOD_SPAN;
        if (scaled < 0) scaled = 0;
        if (scaled > 127) scaled = 127;
        int16_t v = (int16_t)scaled;

        if (wheel_first[1]) {
            wheel_first[1] = false;
            wheel_val[1] = v;
        } else if (v != wheel_val[1]) {
            wheel_val[1] = v;
            hal_input_dispatch_wheel(SMK_WHEEL_MOD, v);
        }
    }
}

static void process_fader_controls(void)
{
    for (uint8_t f = 0; f < SMK_NUM_FADERS; f++) {
        if (adc_fader_gen[f] == fader_last_gen[f]) continue;
        fader_last_gen[f] = adc_fader_gen[f];

        uint16_t raw = adc_fader_val[f];
        fader_accum[f] += raw;
        fader_count[f]++;

        if (fader_count[f] >= 8) {
            uint16_t avg = (uint16_t)(fader_accum[f] >> 3);
            fader_accum[f] = 0;
            fader_count[f] = 0;

            uint8_t v = (uint8_t)(((uint32_t)avg * 127u + 512u) / 1023u);
            if (v > 127) v = 127;

            int diff = (int)v - (int)fader_published[f];
            if (diff < 0) diff = -diff;

            if (fader_first[f]) {
                fader_first[f] = false;
                fader_published[f] = v;
            } else if (diff >= FADER_DEADBAND) {
                fader_published[f] = v;
                hal_input_dispatch_fader(f, v);
            }
        }
    }
}

static void process_analog_controls(void)
{
    process_wheel_controls();
    process_fader_controls();
}

/* =========================================================================
 * Core 0 Keybed Scan Task
 * ========================================================================= */
static void vKeybedScanTask(void *pvParameters)
{
    (void)pvParameters;

    uint32_t last_frame = (uint32_t)-1; /* force first poll to process */

    while (1) {
        const volatile uint8_t *frame;

        /* vTaskDelay(1) yields 1 tick (10 ms) so vGuiTask (Priority 2) can render smoothly.
         * Real-time events (keys, encoders, pad strikes/aftertouch) are captured
         * and queued into lock-free rings at ~2 kHz by soft2_matrix_isr, and drained here. */
        vTaskDelay(1);
        drain_key_events();
        drain_encoder_events();
        drain_pad_events();

        if (scan_frame_cnt == last_frame) {
            continue; /* no new frame since last wake */
        }
        last_frame = scan_frame_cnt;
        uint8_t pub = pub_buf;
        frame = matrix_raw[pub];
        process_buttons(frame);

        /* Analog controls (wheels / faders). */
        process_analog_controls();

        /* Periodic debug telemetry (~1s).
         *
         * OFF by default. This line has repeatedly interleaved with the boot
         * benchmark report and truncated it, costing several rounds of
         * re-measurement for no diagnostic value. Flip to 1 only when the ADC
         * values are actually being investigated, and do not leave it on while
         * capturing boot timings. */
#ifndef ADC_TELEMETRY_ENABLE
#define ADC_TELEMETRY_ENABLE 0
#endif
#if ADC_TELEMETRY_ENABLE
        static uint32_t s_telemetry_tick = 0;
        if (++s_telemetry_tick >= 100) {
            s_telemetry_tick = 0;
            printf("[ADC] P0:%u P1:%u W0:%u W1:%u F0:%u F1:%u F2:%u F3:%u\n",
                   adc_pad_val[0], adc_pad_val[1],
                   adc_wheel_val[0], adc_wheel_val[1],
                   adc_fader_val[0], adc_fader_val[1],
                   adc_fader_val[2], adc_fader_val[3]);
        }
#endif
    }
}

/* =========================================================================
 * Hardware Initialization
 * ========================================================================= */
int mcu_input_init(void)
{
    key_evt_head = 0;
    key_evt_tail = 0;
    enc_evt_head = 0;
    enc_evt_tail = 0;
    memset(key_track, 0, sizeof(key_track));
    memset((void *)matrix_raw, 0xFF, sizeof(matrix_raw));
    fill_buf = 0;
    pub_buf = 0;

    JL_SPI2->CON = 0;
    static const unsigned int out_pins[] = { IO_PORTA_00, IO_PORTA_01, IO_PORTA_03, IO_PORTA_04 };
    for (size_t i = 0; i < sizeof(out_pins) / sizeof(out_pins[0]); i++) {
        gpio_direction_output(out_pins[i], 0);
        gpio_set_die(out_pins[i], 1);
        gpio_set_pull_up(out_pins[i], 0);
        gpio_set_pull_down(out_pins[i], 0);
    }

    static const unsigned int porta_cols[] = { IO_PORTA_02, IO_PORTA_05, IO_PORTA_06, IO_PORTA_07, IO_PORTA_09 };
    for (size_t i = 0; i < sizeof(porta_cols) / sizeof(porta_cols[0]); i++) {
        gpio_direction_input(porta_cols[i]);
        gpio_set_die(porta_cols[i], 1);
        gpio_set_pull_up(porta_cols[i], 1);
        gpio_set_pull_down(porta_cols[i], 0);
    }

    static const unsigned int porth_cols[] = { IO_PORTH_06, IO_PORTH_09, IO_PORTH_08 };
    for (size_t i = 0; i < sizeof(porth_cols) / sizeof(porth_cols[0]); i++) {
        gpio_direction_input(porth_cols[i]);
        gpio_set_die(porth_cols[i], 1);
        gpio_set_pull_up(porth_cols[i], 1);
        gpio_set_pull_down(porth_cols[i], 0);
    }

    static const unsigned int adc_inputs[] = { IO_PORTA_08, IO_PORTC_09, IO_PORTA_10 };
    for (size_t i = 0; i < sizeof(adc_inputs) / sizeof(adc_inputs[0]); i++) {
        gpio_direction_input(adc_inputs[i]);
        gpio_set_die(adc_inputs[i], 0);
        gpio_set_pull_up(adc_inputs[i], 0);
        gpio_set_pull_down(adc_inputs[i], 0);
    }

    analog_phase = 0;
    fader_sub_slot = 0;
    current_adc_ch = AD_CH_PA08;
    scan_step = 0;

    /* Seed resting baseline for all 16 pads to 100 (matches stock 0x02004F36) */
    for (uint8_t p = 0; p < SMK_NUM_PADS; p++) {
        pad_slot_state[p].baseline = 100;
    }

    /* Initialize special pad configuration register (0x00016A0C) for lamp bank selects:
     * Bit 17: Lamp Bank 0 (pad 0x96, active low)
     * Bit 18: Lamp Bank 1 (pad 0x97, active low)
     * Both banks disabled (high) on boot */
    HUSB_PHY0_CON0 = (HUSB_PHY0_CON0 | 0x1000F) | (1 << 17) | (1 << 18);
    lamp_mask = 0;
    update_lamp_bank_words();

    /* Initialize Byte 0 of all 16 row patterns with phase 0 mux code and default lamp state */
    uint8_t init_mux_byte0 = (uint8_t)(((0 & 7) << 3) | 0xC7);
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        row_shift_patterns[r] = (row_shift_patterns[r] & 0xFFFF0000) | 0xFFC0 | init_mux_byte0;
    }

    /* Arm stock 3-step SAR-ADC interlock (AdvancePadMatrixScanPhaseIsr @ 0x0201D76C) */
    scan_interlock_state = 1;

    /* Enable Bandgap Reference Buffer and PMU detection in P33 for SAR-ADC */
    P33_CON_SET(P3_ANA_CON4, 5, 1, 1); /* VBG_BUF_EN(1) */
    P33_CON_SET(P3_ANA_CON4, 0, 1, 1); /* PMU_DET_EN(1) */
    P33_CON_SET(P3_ANA_CON4, 1, 3, 0); /* ADC_CHANNEL_SEL(0) = VBG */

    /* Enable Analog Bias Generator in WLA_CON0 and disable PLL test mode */
    JL_ANA->PLL_CON1 &= ~(1 << 16);     /* SYSPLL1_TEST_EN_12v = 0 */
    JL_ANA->WLA_CON0 = (JL_ANA->WLA_CON0 & ~0x380FE) | (1 << 0) | (0x2 << 1) | (1 << 14) | (0b100 << 15);
    /* Route SPI2 to Group C (PA3=CLK, PA4=DO, PA2=DI) -> bit 17 set (0x20000) */
    JL_IOMAP->CON1 = (JL_IOMAP->CON1 & ~(3 << 16)) | (2 << 16);

    JL_SPI2->BAUD = (uint32_t)SPI2_BAUD_DIVISOR;

    request_irq(IRQ_SARADC_IDX, 3, saradc_analog_isr, 0);
    request_irq(IRQ_SPI2_IDX, 5, spi2_dma_matrix_isr, 0);
    request_irq(IRQ_SOFT2_IDX, 4, soft2_matrix_isr, 0);

    JL_SPI2->ADR = (uint32_t)&row_shift_patterns[0];
    JL_SPI2->CON = 0x6021;
    JL_SPI2->CNT = 4;

    xTaskCreate(
        vKeybedScanTask,
        "#C0keybed_scan",
        1024,
        NULL,
        8,
        NULL
    );

    return 0;
}