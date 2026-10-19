/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_main.c
 * SMK-37 Pro Custom Firmware MCU Entry Point (JieLi AC791N PI32v2)
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "asm/cpu.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lvgl.h"
#include "hal_display.h"
#include "hal_audio.h"
#include "hal_time.h"
#include "hal_input.h"
#include "st7789.h"
#include "diag.h"
#include "app_main.h"
#include "mcu_usb.h"
#include "mcu_ota.h"

#include "asm/p33.h"
#include "asm/clock.h"
#include "asm/adc_api.h"
#include "driver/device/gpio.h"
#include "selftest.h"
#include "bench.h"

/* Sustained-load benchmark mode: holds a worst-case chord and reports the
 * per-block CPU distribution over CDC. Mutually exclusive with the boot-time
 * microbenchmarks, which cost boot time and code that this mode reclaims. */
#if defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE
#include "bench_load.h"
#endif

extern int mcu_input_init(void);

/* Firmware version string */
const char FIRMWARE_VERSION[] = "SMK-37 Pro_001";

/* Watchdog timer controls from vendor driver */
void wdt_close(void);
void wdt_clear(void);

/* Core 1 SRAM Reset Vector */
#define CORE1_RESET_VECTOR_ADDR         ((volatile uint32_t *)0x01C7FFF8)
#define CORE1_RESET_VECTOR_CACHE_LINE   ((void *)0x01C7FFE0)

/*
 * Unbuffered boot diagnostics.
 *
 * This SDK provides printf but no fflush, so stdout buffering is undefined and
 * boot markers cannot be trusted: the tail of "[P4 core1 up]" was lost to the
 * ADC telemetry flood. putchar() is the SDK's primitive debug-UART write
 * (declared in system/generic/printf.h) and goes out immediately, so the core 1
 * verdict is emitted as a short token repeated several times. Any surviving copy
 * is enough, which makes the result readable regardless of FIFO loss -- and
 * this verdict is what decides whether audio runs on one core or two.
 */
static void boot_puts(const char *s)
{
    while (*s != '\0') {
        putchar((int)(unsigned char)*s++);
    }
}

/* =========================================================================
 * Core 1 Performance Profiling (Hardware Timer 5 @ 24 MHz)
 * ========================================================================= */
/* 128 frames @ 48 kHz = 2.6667 ms. At 24 MHz crystal reference,
 * 1 audio block deadline = (128 / 48000) * 24,000,000 = 64,000 timer ticks. */
#define AUDIO_BLOCK_TICKS_24M   64000

static uint32_t core1_block_start = 0;
static uint32_t core1_peak_ticks = 0;

static inline void core1_perf_block_start(void)
{
    core1_block_start = JL_TIMER4->CNT;
}

static inline void core1_perf_block_end(void)
{
    uint32_t elapsed = JL_TIMER4->CNT - core1_block_start;
    if (elapsed > core1_peak_ticks) {
        core1_peak_ticks = elapsed;
    }
}

/**
 * @brief Returns the peak Core 1 CPU load percentage (0 - 100%) across recent audio blocks.
 *
 * Each 128-frame audio block has a real-time deadline of 64,000 ticks (2.667 ms @ 24 MHz).
 * Load % = (peak_active_ticks * 100) / 64000 = peak_active_ticks / 640.
 * Headroom % = 100 - load%.
 */
__attribute__((unused))
static uint8_t core1_get_cpu_load(void)
{
    /* Include active rendering time up to this point in the current block */
    core1_perf_block_end();

    uint16_t peak = core1_peak_ticks;
    core1_peak_ticks = 0;

    uint32_t load = (uint32_t)peak / (AUDIO_BLOCK_TICKS_24M / 100);
    if (load > 100) {
        load = 100;
    }
    return (uint8_t)load;
}

/**
 * @brief Returns available Core 1 DSP headroom in microseconds for the worst-case block.
 */
__attribute__((unused))
static inline uint16_t core1_get_headroom_us(void)
{
    uint16_t peak = core1_peak_ticks;
    if (peak >= AUDIO_BLOCK_TICKS_24M) {
        return 0;
    }
    return (uint16_t)((AUDIO_BLOCK_TICKS_24M - peak) / 24);
}

/* =========================================================================
 * System Clock & Voltage Configuration
 * ========================================================================= */
extern void pll2_init(uint32_t os_freq, uint32_t sys_clk, uint8_t aut_clk, uint8_t sys_div, uint8_t sdram_clk);
extern void sys_clk_source_rc_clk_on(int is_16m);
extern void sys_clk_source_rc_clk_off(void);

__attribute__((section(".volatile_ram_code"), noinline))
static void clock_init(void)
{
    /* 1. Scale core voltages BEFORE increasing frequency:
     *    SYSVDD (Core / DSP):
     *      - 320 MHz: level 11 (1.26 V, SYSVDD_VOL_SEL_126V)
     *      - 360 MHz: level 14 (1.35 V, SYSVDD_VOL_SEL_135V) (P3_ANA_CON9[3:0] = 14 = 0x0E)
     *    VDC14:
     *      - 320 MHz: level 4  (1.45 V, VDC14_VOL_SEL_145V) - JieLi official recommendation
     *      - 360 MHz: level 5  (1.50 V, VDC14_VOL_SEL_150V) - High-speed margin
     *    VDDIOM (IO Main)    -> 3.2 V  (P3_ANA_CON5[2:0] = 3  = 0x03)
     *    VDDIOW (IO West/PH) -> 3.2 V  (P3_ANA_CON5[5:4] = 3  = 0x03)
     */
#if defined(CPU_FREQ_MHZ) && (CPU_FREQ_MHZ == 360)
    P33_CON_SET(0x09, 0, 4, 14);
    P33_CON_SET(0x06, 0, 3, 5);
#else
    P33_CON_SET(0x09, 0, 4, 11);
    P33_CON_SET(0x06, 0, 3, 4);
#endif
    P33_CON_SET(0x05, 0, 3, 3);
    P33_CON_SET(0x05, 4, 2, 3);

    /* Voltage stabilization delay (~1000 NOPs) */
    for (volatile int i = 0; i < 1000; i++) {
        __asm__ volatile("nop");
    }

    /* 2. Configure bus dividers in JL_CLOCK->SYS_DIV:
     *    - HSB: div 2 (bit[17:16]=1) -> 160 MHz @ 320M, 180 MHz @ 360M
     *    - LSB: div 4 (bit[10:8]=3)  ->  40 MHz @ 320M,  45 MHz @ 360M
     *    - SFC: div 4 (bit[14:12]=3) ->  40 MHz @ 320M,  45 MHz @ 360M
     */
    JL_CLOCK->SYS_DIV = (JL_CLOCK->SYS_DIV & ~0x37700) | (1 << 16) | (3 << 8) | (3 << 12);

    /* 3. PLL Configuration and Switching */
#if defined(CPU_FREQ_MHZ) && (CPU_FREQ_MHZ == 360)
    /* Switch to internal RC clock before reconfiguring PLL/dividers to prevent clock glitching */
    sys_clk_source_rc_clk_on(1);

    /* Initialize PLL2 to 540 MHz via vendor SDK pll2_init:
     *   os_freq:   24,000,000 Hz (crystal)
     *   sys_clk:   360,000,000 Hz
     *   aut_clk:   1 (derive PLL2 frequency: sys_clk * sys_div / 2 = 540 MHz)
     *   sys_div:   3 (branch 6: 540 MHz / 1.5 = 360 MHz)
     *   sdram_clk: 0
     * This safely configures PLL2 analog bias, loop filter, charge pump,
     * reference divider (2 MHz PFD), VCO multiplier (270 = 540 MHz),
     * enables analog power, and opens the clock output gate.
     */
    pll2_init(24000000, 360000000, 1, 3, 0);

    /* Branch 6: PLL2 (540 MHz) / 1.5 = 360 MHz */
    JL_CLOCK->CLK_CON2 = (JL_CLOCK->CLK_CON2 & ~0xFF) | 0x06;

    for (volatile int i = 0; i < 1000; i++) {
        __asm__ volatile("nop");
    }

    /* Switch back from RC to PLL clock */
    sys_clk_source_rc_clk_off();
#else
    /* PLL System output: branch 2 = 320 MHz undivided (div0=/1, div1=/1) */
    JL_CLOCK->CLK_CON2 = (JL_CLOCK->CLK_CON2 & ~0xFF) | 0x02;
#endif

    /* 5. Switch main system clock source to PLL */
    JL_CLOCK->CLK_CON0 = (JL_CLOCK->CLK_CON0 & ~(7 << 6)) | (7 << 6);

    /* 6. Energize Analog Audio PLL clock domain power gate (JL_ANA->PLL_CON1 bit 29).
     * Matches ROM InitializeSystemAndStartRtos @0x02002252 to ensure the ALNK/I2S
     * clock tree is powered for the audio subsystem at boot. */
    JL_ANA->PLL_CON1 |= (1 << 29);

    __asm__ volatile("csync");
}

/* Mirrors the ISR_ST_* diagnostic bits defined in mcu_input.c (no shared
 * header; kept local so the native build is untouched). */
#define ISR_ST_MATRIX  0x01
#define ISR_ST_FLUSH   0x02
#define ISR_ST_ADC     0x04
#define ISR_ST_SEM     0x08

extern volatile uint8_t isr_stage;

static void gui_task(void *pv_parameters)
{
    (void)pv_parameters;

    /* Re-synchronize Tick Timer (TTMR) to 320 MHz root clock domain.
     * xPortStartScheduler() divided clk_get("sys") (360 MHz) by configTICK_RATE_HZ,
     * but TTMR is clocked from the primary 320 MHz PLL1 root domain.
     * Setting TTMR_PRD to SYS_CLK_320M / configTICK_RATE_HZ ensures accurate 10 ms (100 Hz) tick pacing. */
    q32DSP(0)->TTMR_PRD = (uint32_t)(SYS_CLK_320M / configTICK_RATE_HZ);

    diag_set_flag(DIAG_FLAG_GUI_TASK_ENTRY);

    /* 1. Initialize LVGL */
    lv_init();
    diag_set_flag(DIAG_FLAG_LVGL_INIT_DONE);

    /* 2. Initialize display hardware HAL */
    hal_display_init();
    diag_set_flag(DIAG_FLAG_HAL_DISP_INIT_DONE);

    /* 3. Initialize application (synth, UI, input listeners) */
    app_init();
    diag_set_flag(DIAG_FLAG_APP_INIT_DONE);

    /* 4. Initialize USB subsystem after UI and audio are up */
    mcu_usb_init();

    isr_stage = ISR_ST_MATRIX | ISR_ST_ADC;

    uint32_t last_tick = hal_get_ms();
    bool first_loop = true;

    while (1) {
        if (first_loop) {
            diag_set_flag(DIAG_FLAG_GUI_LOOP_FIRST_TICK);
            first_loop = false;
        }

        uint32_t current_tick = hal_get_ms();
        uint32_t elapsed = current_tick - last_tick;
        if (elapsed > 0) {
            lv_tick_inc(elapsed);
            last_tick = current_tick;
        }

        lv_timer_handler();
        app_step();
        hal_input_poll();
        mcu_usb_poll();
        mcu_ota_check_pending();

#if defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE
        /* Drain benchmark statistics here rather than on the audio core. The
         * render loop is close enough to its deadline at six worst-case voices
         * that formatting and transmitting a line inside it costs real CPU;
         * this task has 10 ms of slack and already polls the CDC. */
        bench_load_report_print();
#endif

        vTaskDelay(1); // 1 tick = 10 ms pacing
    }
}

/* Core 1 Secondary CPU Boot & Handshake State (32-byte cache-line aligned) */
#define C1_BOOT_RESET   0
#define C1_BOOT_RUNNING 1
#define C1_BOOT_READY   2

static volatile uint32_t cpu1_boot_flag __attribute__((aligned(32))) = C1_BOOT_RESET;

/* Vendor vm_sfc.c expects cpu1_run_flag symbol; kept 0 so SFC does not pause RAM-only Core 1 */
volatile char cpu1_run_flag = 0;

extern void interrupt_init(void);
extern void cpu1_start(void);

#include "spsc_ring.h"
#include "hal_synth.h"
#include "synth_engine.h"
#include "patch_banks.h"
#include "hal_audio.h"
#include "hal_midi.h"
#include "mcu_audio.h"

extern volatile uint8_t audio_render_ready;
extern volatile uint8_t audio_render_subbuf;

__attribute__((section(".volatile_ram_code")))
void cpu1_entry(void)
{
    /* 1. Disable interrupts during Core 1 on-core hardware configuration */
    __builtin_pi32v2_cli();

    /* 2. Signal Core 0 that Core 1 is awake and entering initialization */
    cpu1_boot_flag = C1_BOOT_RUNNING;
    __asm__ volatile("ssync; csync" ::: "memory");

    /* 3. Initialize synthesizer engine on Core 1 (owns Dexed heap & state in its DCache).
     * Interrupts remain disabled during synth_init so alnk_isr cannot interrupt during
     * memory allocation or table decoding. */
    synth_init(AUDIO_DEFAULT_SAMPLE_RATE);

#if defined(STARTUP_DIAGNOSTICS) && STARTUP_DIAGNOSTICS
    /* 3a. Time the real Juno engine. Must run after synth_init() -- bench_run()
     * is too early, the engines do not exist yet (measured synth_init=0,
     * s_juno=NULL there) -- and before vTaskStartScheduler(), so no task can
     * perturb a timed region. */
    bench_juno_full_sweep();
    synth_set_engine(SYNTH_ENGINE_DEXED);
#endif

#if defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE
    /* 3a-bis. Install the sustained-load benchmark. Must also run after
     * synth_init() and before the first audio block, so the measured voice
     * count is exactly the chord and not whatever the boot sequence left. */
    bench_load_init();
#endif

    /* 4. Initialize TIMER4 as free-running 32-bit 24 MHz counter for microsecond load profiling */
    JL_TIMER4->CON = (1 << 14);              /* Clear pending flag */
    JL_TIMER4->CNT = 0;
    JL_TIMER4->PRD = 0xFFFFFFFF;             /* Free-running 32-bit auto-reload (wraps at 178.9 s) */
    JL_TIMER4->CON = (0x2 << 2) | (1 << 0);  /* 24 MHz OSC, periodic mode, timer enable */

    /* 5. Enable interrupts on Core 1 (for audio DMA interrupts etc.) */
    __builtin_pi32v2_sti();

    /* 6. Signal Core 0 that Core 1 is fully initialized and entering DSP superloop */
    cpu1_boot_flag = C1_BOOT_READY;
    __asm__ volatile("ssync; csync" ::: "memory");

    /* 7. Dedicated Bare-Metal Real-Time Audio DSP Superloop (in SRAM on Core 1) */
#define AUDIO_SUB_BLOCK_FRAMES 32
#define AUDIO_NUM_SUB_BLOCKS 4
    int16_t q_pcm[AUDIO_SUB_BLOCK_FRAMES * 2];
#if !(defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE)
    static uint8_t vu_div = 0;
    static uint8_t last_vu = 0xFF;
    float current_bender = 0.0f;
    float target_bender = 0.0f;
    float current_mod_wheel = 0.0f;
    float target_mod_wheel = 0.0f;
    float current_volume = 1.0f;
    float target_volume = 1.0f;
    float current_aftertouch = 0.0f;
    float target_aftertouch = 0.0f;
#endif

    while (1) {
        if (!audio_render_ready) {
            __asm__ volatile("idle");
        }

        if (audio_render_ready) {
            audio_render_ready = 0;
            core1_perf_block_start();
            uint8_t subbuf = audio_render_subbuf;
            int32_t *dma_dst = mcu_audio_get_subbuf_ptr(subbuf);

            /* Drain pending events from lock-free SPSC ring ASAP */
            SpscEvent evt;
            while (spsc_ring_pop(&core_event_ring, &evt)) {
#if !(defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE)
                switch (evt.type) {
                case SPSC_EVT_NOTE_ON:
                    synth_note_on(evt.data1, evt.data2);
                    break;
                case SPSC_EVT_NOTE_OFF:
                    synth_note_off(evt.data1);
                    break;
                case SPSC_EVT_ALL_NOTES_OFF:
                    synth_all_notes_off();
                    break;
                case SPSC_EVT_PITCH_BEND:
                    target_bender = (float)evt.svalue16 / 8192.0f;
                    break;
                case SPSC_EVT_CC:
                    if (evt.data1 == 1) {
                        target_mod_wheel = (float)evt.data2 / 127.0f;
                    } else if (evt.data1 == 7) {
                        target_volume = (float)evt.data2 / 100.0f;
                    }
                    break;
                case SPSC_EVT_AFTERTOUCH:
                    target_aftertouch = (float)evt.data1 / 127.0f;
                    break;
                case SPSC_EVT_PROGRAM_CHANGE:
                    synth_set_bank_patch(evt.data1, evt.data2);
                    break;
                case SPSC_EVT_ENGINE_SELECT:
                    synth_set_engine(evt.data1);
                    current_bender = target_bender;
                    current_mod_wheel = target_mod_wheel;
                    current_volume = target_volume;
                    current_aftertouch = target_aftertouch;
                    synth_set_pitch_bend_f(current_bender);
                    synth_set_mod_wheel_f(current_mod_wheel);
                    synth_set_volume_f(current_volume);
                    synth_set_aftertouch_f(current_aftertouch);
                    break;
                case SPSC_EVT_PARAM_CHANGE: {
                    float val = 0.0f;
                    uint8_t pid = evt.channel;
                    if (pid == 0 || pid == 32) {
                        val = (float)evt.uvalue16 / 100.0f;
                    } else if (pid == 1) {
                        val = (float)evt.uvalue16 / 1000.0f;
                    } else if (pid == 15) {
                        val = (float)evt.svalue16;
                    } else if (pid == 4 || pid == 11 || pid == 18 || pid == 24 || pid == 29 || pid == 31 || pid == 33 || pid == 34) {
                        val = (float)evt.uvalue16;
                    } else {
                        val = (float)evt.uvalue16 / 10000.0f;
                    }
                    synth_set_custom_param(pid, val);
                    break;
                }
                case SPSC_EVT_LIMITER: {
                    float t = (float)evt.data1 / 100.0f;
                    float m = (float)evt.data2 / 10.0f;
                    synth_set_limiter(t, m);
                    break;
                }
                default:
                    break;
                }
#endif
            }

            /* Sub-block processing (four 32-sample quanta) with linear parameter slewing */
#if !(defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE)
            float step_bender = (target_bender - current_bender) * 0.25f;
            float step_mod_wheel = (target_mod_wheel - current_mod_wheel) * 0.25f;
            float step_volume = (target_volume - current_volume) * 0.25f;
            float step_aftertouch = (target_aftertouch - current_aftertouch) * 0.25f;
            bool has_bender_slew = (target_bender != current_bender);
            bool has_mod_wheel_slew = (target_mod_wheel != current_mod_wheel);
            bool has_volume_slew = (target_volume != current_volume);
            bool has_aftertouch_slew = (target_aftertouch != current_aftertouch);
#endif

            for (uint8_t q = 0; q < AUDIO_NUM_SUB_BLOCKS; q++) {
#if !(defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE)
                if (q == AUDIO_NUM_SUB_BLOCKS - 1) {
                    current_bender = target_bender;
                    current_mod_wheel = target_mod_wheel;
                    current_volume = target_volume;
                    current_aftertouch = target_aftertouch;
                } else {
                    current_bender += step_bender;
                    current_mod_wheel += step_mod_wheel;
                    current_volume += step_volume;
                    current_aftertouch += step_aftertouch;
                }

                if (has_bender_slew) {
                    synth_set_pitch_bend_f(current_bender);
                }
                if (has_mod_wheel_slew) {
                    synth_set_mod_wheel_f(current_mod_wheel);
                }
                if (has_volume_slew) {
                    synth_set_volume_f(current_volume);
                }
                if (has_aftertouch_slew) {
                    synth_set_aftertouch_f(current_aftertouch);
                }
#endif
                synth_get_samples(q_pcm, AUDIO_SUB_BLOCK_FRAMES);

                /* Decoupled USB Audio Streaming: push 32 frames to cache-safe SPSC ring if host recording */
                if (hal_usb_audio_is_active()) {
                    hal_usb_audio_push_samples(q_pcm, AUDIO_SUB_BLOCK_FRAMES);
                }

                /* Format-convert and scale directly into DMA sub-buffer */
                mcu_audio_render_quantum(dma_dst + (q * AUDIO_SUB_BLOCK_FRAMES * 2),
                                         q_pcm, AUDIO_SUB_BLOCK_FRAMES);
            }

#if defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE
            /* Account the render immediately, before the benchmark's own
             * bookkeeping and zero-fill run, so the reported figure is the
             * render cost alone. The serial report is the only output in this
             * build, which is why the VU publish below is compiled out. */
            core1_perf_block_end();
            bench_load_on_block(JL_TIMER4->CNT - core1_block_start,
                                NULL, 128);
            memset(dma_dst, 0, AUDIO_DMA_SUB_WORDS * sizeof(int32_t));
#endif

#if !(defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE)
            /* Publish Core 1 CPU load / headroom feedback to Core 0 periodically (~21.3ms) when changed */
            if (++vu_div >= 8) {
                vu_div = 0;
                uint8_t current_vu = core1_get_cpu_load();
                if (current_vu != last_vu) {
                    last_vu = current_vu;
                    synth_feedback_write(&synth_feedback, current_vu);
                }
            }
#endif

            /* Commit DMA sub-buffer and flush cache */
            mcu_audio_flush_subbuf(subbuf);
#if !(defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE)
            core1_perf_block_end();
#endif
        }
    }
}

int main(void)
{
    /* 0. Warm-Boot Peripheral & Multi-Core Clean Reset:
     * When running via tools/run_ram.sh, the SoC is not power-cycled.
     * Core 1, ALNK0 DMA, and SPI2 DMA from the previous run may still be
     * active and executing from SRAM, which corrupts bus arbitration and causes
     * ST7789 display initialization commands (MADCTL 0x36, INVON 0x21) to be dropped.
     * Halt Core 1 and all previous DMA engines immediately before anything else. */
    corex2(0)->C1_CON |= 0x2;   /* Put Core 1 into reset */
    corex2(0)->C1_CON &= ~0x8;  /* Disable Core 1 clock */
    JL_ALNK0->CON0 = 0;         /* Stop ALNK0 CON0 DMA */
    JL_ALNK0->CON2 = 0x0F;      /* Clear ALNK0 CON2 pending */
    JL_SPI2->CON = 0;           /* Stop SPI2 CON DMA */
    JL_TIMER4->CON = 0;         /* Stop TIMER4 */
    JL_TIMER5->CON = 0;         /* Stop TIMER5 */
    /* Clamp PC10 (WS2812 DIN) low with internal pull-down to eliminate power-on/reboot line glitches */
    gpio_direction_output(IO_PORTC_10, 0);
    gpio_set_die(IO_PORTC_10, 1);
    gpio_set_pull_up(IO_PORTC_10, 0);
    gpio_set_pull_down(IO_PORTC_10, 1);
    __asm__ volatile("csync");

    /* 1. Disable hardware watchdog */
    wdt_close();

    /* 2. Initialize Core 0 interrupt controller */
    interrupt_init();

    /* 3. Scale voltages and switch clock to target frequency (in RAM) */
    void (*volatile p_clock_init)(void) = clock_init;
    p_clock_init();
    clk_early_init();

    /* 3. Initialize 24 MHz hardware time base (Timer1) */
    hal_time_init();

#if !defined(TARGET_RAM)
    /* 3b. Clean up leftover OTA staging flash if present */
    mcu_ota_boot_cleanup();
#endif

    /* 4. Initialize ST7789 panel */
    st7789_init();

#if defined(STARTUP_DIAGNOSTICS) && STARTUP_DIAGNOSTICS
    st7789_backlight(1);

    /* 5. Initialize structured visual diagnostics canvas */
    diag_init();
    diag_set_flag(DIAG_FLAG_MAIN_ENTRY);
    diag_set_flag(DIAG_FLAG_WDT_DISABLED);
    diag_set_flag(DIAG_FLAG_INT_INIT_DONE);
    diag_set_flag(DIAG_FLAG_CLOCK_320M_DONE);
    diag_set_flag(DIAG_FLAG_TIME_INIT_DONE);
    diag_set_flag(DIAG_FLAG_ST7789_INIT_DONE);

#if !(defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE)
    selftest_run();
#endif

    bench_run();
#if !(defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE)
    printf("[P0 bench returned]\n");
#endif
    wdt_clear();
#endif /* STARTUP_DIAGNOSTICS */

    /* 5c. Initialize Synthesizer Subsystem & Cross-Core IPC Concentrator */
    hal_synth_init(AUDIO_SAMPLE_RATE);
#if defined(STARTUP_DIAGNOSTICS) && STARTUP_DIAGNOSTICS
#if !(defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE)
    printf("[P1 ipc]\n");
#endif
#endif

    /* 6. Register input listeners BEFORE the keybed scanner starts */
    app_register_input();
    diag_set_flag(DIAG_FLAG_INPUT_PRE_INIT);
#if defined(STARTUP_DIAGNOSTICS) && STARTUP_DIAGNOSTICS
#if !(defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE)
    printf("[P2 input-reg]\n");
#endif
#endif

    /* 7. Initialize physical matrix keybed scanner (Task Priority: 10 - High Priority) */
    mcu_input_init();
    diag_set_flag(DIAG_FLAG_MCU_INPUT_INIT_DONE);
#if defined(STARTUP_DIAGNOSTICS) && STARTUP_DIAGNOSTICS
#if !(defined(MCUNO_BENCH_MODE) && MCUNO_BENCH_MODE)
    printf("[P3 mcu-input]\n");
#endif
#endif

    /* 8. Create FreeRTOS GUI Task on Core 0.
     * Stack: 1024 words (4KB) was too small for LVGL object creation -
     * ui_sample_init crashed/reset at the first lv_label_create()
     * (Decode Idx B342A9: Row 4 = R Y G C <died before col4>, Row 3 Col 6
     * Blue = hal_audio_init done). configCHECK_FOR_STACK_OVERFLOW=0 means an
     * overflow just corrupts memory instead of tripping a hook -> bus fault
     * -> instant reset. 4096 words = 16KB headroom for LVGL + render. */
    xTaskCreate(gui_task, "#C0gui_task", 4096, NULL, 2, NULL);
    diag_set_flag(DIAG_FLAG_GUI_TASK_CREATED);

    /* Telemetry task disabled during audio bringup to eliminate ADC / PMU noise on analog rails */
    // xTaskCreate(vTelemetryTask, "#C0telem_task", 512, NULL, 1, NULL);

    /* Pre-populate Core 1 reset vector in physical SRAM */
    *CORE1_RESET_VECTOR_ADDR = (uint32_t)cpu1_start;
    diag_set_flag(DIAG_FLAG_CORE1_VEC_FLUSHED);

    /* Release Core 1 from hardware reset register. Drain store buffer before unresetting. */
    cpu1_boot_flag = C1_BOOT_RESET;
    __asm__ volatile("ssync; csync" ::: "memory");

    corex2(0)->C1_CON |= 0x8;
    corex2(0)->C1_CON &= ~0x2;

    /* Wait for Core 1 to signal entry into bare-metal audio loop.
     *
     * Core 1 performs synth_init() and on-core hardware configuration before
     * setting cpu1_boot_flag to C1_BOOT_READY. Cache lines are explicitly
     * invalidated on Core 0 so that Core 1's updates in physical SRAM are
     * immediately visible.
     */
    {
        int handshake_ok = 0;
        for (int attempt = 0; attempt < 3 && !handshake_ok; attempt++) {
            /* ~1 s per attempt at this csync rate, WDT-fed throughout. */
            for (uint32_t spin = 0; spin < 20000000u; spin++) {
                __asm__ volatile("csync" ::: "memory");
                if (cpu1_boot_flag == C1_BOOT_READY) {
                    handshake_ok = 1;
                    break;
                }
                if ((spin & 0xFFF) == 0) {
                    wdt_clear();
                }
            }
            if (!handshake_ok) {
                printf("[C1=TIMEOUT flag=%u]\n", (unsigned)cpu1_boot_flag);
            }
        }
        if (handshake_ok) {
            boot_puts("[C1=UP]\n[C1=UP]\n[C1=UP]\n");
        } else {
            /* Core 0 only: audio capacity is halved and the 6-voice target
             * cannot be met until this is resolved. */
            boot_puts("[C1=DOWN]\n[C1=DOWN]\n[C1=DOWN]\n");
            diag_set_flag(DIAG_FLAG_CORE1_VEC_FLUSHED);
        }
    }

    /* 9. Start FreeRTOS Kernel Scheduler on Core 0 (Single-Core Mode) */
    extern volatile int scheduler_started;
    scheduler_started = 1;
    diag_set_flag(DIAG_FLAG_SCHEDULER_START);
    vTaskStartScheduler();

    while (1) {
        __asm__ volatile("idle");
    }

    return 0;
}
