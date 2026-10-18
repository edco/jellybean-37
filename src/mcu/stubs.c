/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file stubs.c
 * System and OS hooks, platform tables, and runtime support for JieLi FreeRTOS and drivers.
 *
 * This file provides the board-support and application-layer hooks required by the JieLi
 * PI32v2 vendor libraries (system.a and cpu.a), along with lightweight stubs for optional
 * subsystems that are not utilized on the SMK-37 Pro hardware.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "asm/cpu.h"
#include "asm/wdt.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "semphr.h"
#include "os/os_api.h"
#include "system/task.h"

/* -------------------------------------------------------------------------
 * 1. Port R (PR) GPIO Stubs
 * -------------------------------------------------------------------------
 * Reason to keep:
 * The vendor GPIO driver (gpio.c.o) contains dispatch logic for Port R pins
 * (PR0..PR3) which are routed through the RTC/P33 power domain.
 * Linking the vendor rtc.c.o driver to provide these would pull in lrc_hw.c.o
 * and power_hw.c.o, dragging in JieLi's entire low-power PMU and WiFi sleep
 * subsystem. Because the SMK-37 Pro hardware only utilizes PORTA, PORTB,
 * PORTC, PORTD, and PORTH, Port R is never addressed at runtime.
 */
int rtc_port_pr_in(uint8_t port) { (void)port; return 0; }
int rtc_port_pr_read(uint8_t port) { (void)port; return 0; }
int rtc_port_pr_out(uint8_t port, bool on) { (void)port; (void)on; return 0; }
int rtc_port_pr_hd(uint8_t port, bool on) { (void)port; (void)on; return 0; }
int rtc_port_pr_pu(uint8_t port, bool on) { (void)port; (void)on; return 0; }
int rtc_port_pr_pd(uint8_t port, bool on) { (void)port; (void)on; return 0; }
int rtc_port_pr_die(uint8_t port, bool on) { (void)port; (void)on; return 0; }

/* -------------------------------------------------------------------------
 * 2. Assert and Clock Initialization Callbacks
 * -------------------------------------------------------------------------
 * Reason to keep:
 * cpu_assert_debug() is the target function called by the JieLi SDK ASSERT()
 * macro when an assertion check fails.
 * usb_pll_init() is an optional callback referenced by clock.c.o during system
 * clock setup.
 * config_asser controls vendor driver assertion logging (0 = disabled).
 */
void cpu_assert_debug(void)
{
    while (1) {
        /* Halt core on assertion failure */
    }
}


void otg_prevent_detect(uint8_t id)
{
    (void)id;
}

const char log_tag_const_e_USB = 0;
const char log_tag_const_i_USB = 0;
const char log_tag_const_d_USB = 0;
const char log_tag_const_w_USB = 0;

extern void P33_CON_SET(uint16_t reg, uint8_t bit, uint8_t width, uint8_t val);

// Exact copy of SDK function to avoid linking in entire power management subsystem
void p33_soft_reset(void)
{
    /* Exact JieLi SDK hardware reset sequence (register 0xA0, bit 4) */
    P33_CON_SET(0xA0, 4, 1, 1);
    while (1) {
        __asm__ volatile("idle");
    }
}

const int config_asser = 0;

/* -------------------------------------------------------------------------
 * 3. Vendor Subsystem Log Filtering Flags
 * -------------------------------------------------------------------------
 * Reason to keep:
 * JieLi vendor libraries in system.a declare these variables as extern const char
 * (following JieLi's lib_system_config.c architecture). Setting them to 0 silences
 * verbose internal driver debug output while satisfying linker symbol references.
 */
const char log_tag_const_i_OS = 0;
const char log_tag_const_d_OS = 0;
const char log_tag_const_w_OS = 0;
const char log_tag_const_e_OS = 0;
const char log_tag_const_c_OS = 0;

const char log_tag_const_i_SYS_TMR = 0;
const char log_tag_const_d_SYS_TMR = 0;
const char log_tag_const_w_SYS_TMR = 0;
const char log_tag_const_e_SYS_TMR = 0;

const char log_tag_const_i_TMR = 0;
const char log_tag_const_d_TMR = 0;
const char log_tag_const_w_TMR = 0;
const char log_tag_const_e_TMR = 0;

const char log_tag_const_i_HEAP_MEM = 0;
const char log_tag_const_d_HEAP_MEM = 0;
const char log_tag_const_w_HEAP_MEM = 0;
const char log_tag_const_e_HEAP_MEM = 0;

const char log_tag_const_i_WAIT_COMPLETION = 0;
const char log_tag_const_d_WAIT_COMPLETION = 0;
const char log_tag_const_w_WAIT_COMPLETION = 0;
const char log_tag_const_e_WAIT_COMPLETION = 0;

const char log_tag_const_i_KTASK = 0;
const char log_tag_const_e_KTASK = 0;

/* -------------------------------------------------------------------------
 * 4. Hardware IRQ Routing Table
 * -------------------------------------------------------------------------
 * Reason to keep:
 * Required by JieLi interrupt vector manager (hwi.c.o in system.a). Defines
 * peripheral IRQ priority configurations, terminated by 0xFF.
 */
const struct irq_info irq_info_table[] = {
    { 0xFF, 0, 0 }
};

/* -------------------------------------------------------------------------
 * 5. FreeRTOS System Task Configuration Table
 * -------------------------------------------------------------------------
 * Reason to keep:
 * Required by JieLi FreeRTOS initialization. Registers task stack depths,
 * priorities, and message queue sizes. "sys_timer" is required by the JieLi
 * software timer service.
 */
const struct task_info task_info_table[] = {
    {"sys_timer",            9,     512,	  128,   NULL },
    {"#C0gui_task",          3,    1024,        0,   NULL },
    {NULL,                   0,       0,        0,   NULL },
};

/* -------------------------------------------------------------------------
 * 6. Hardware Stack Pointer Monitor Hooks
 * -------------------------------------------------------------------------
 * Reason to keep:
 * Called by JieLi's FreeRTOS port (port.c.o) on context switches to enable
 * or disable hardware SP window bounds checking.
 */
void sp_ovf_unen(void)
{
}

void sp_ovf_en(void)
{
}

/* -------------------------------------------------------------------------
 * 7. Standard C Library Support & Minimal Console I/O
 * -------------------------------------------------------------------------
 * Reason to keep:
 * - errno: Required by toolchain libc / compiler-rt math and string functions.
 * - putchar, log_putbyte, puts, printf, log_print: Minimal console I/O backend
 *   invoked by vendor printf-stdarg.c.o and driver logging routines.
 */
int errno = 0;

#include "mcu_cdc.h"

int putchar(int c)
{
    mcu_cdc_putc((char)c);
    return c;
}

void log_putbyte(char c)
{
    putchar(c);
}

void putbyte(char c)
{
    putchar(c);
}

const char log_tag_const_e_VM = 0;
const char log_tag_const_i_VM = 0;
const char log_tag_const_w_VM = 0;
const char log_tag_const_d_VM = 0;
const int norflash_write_readback_check_en = 0;

void printf_buf(const uint8_t *buf, uint32_t len)
{
    (void)buf;
    (void)len;
}

int puts(const char *s)
{
    if (s) {
        mcu_cdc_puts(s);
        mcu_cdc_putc('\n');
    }
    return 0;
}

extern int vprintf(const char *fmt, va_list ap);

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

void log_print(int level, const char *tag, const char *fmt, ...)
{
    (void)level;
    (void)tag;
    (void)fmt;
}

/* -------------------------------------------------------------------------
 * 8. System Tick Counter & Core CPU Status Helpers
 * -------------------------------------------------------------------------
 * Reason to keep:
 * - jiffies: Global tick counter referenced by vendor timer.c.o and port.c.o.
 * - scheduler_started: Indicates whether the FreeRTOS scheduler is running.
 * - cpu_in_irq() & cpu_irq_disabled(): Read the PI32v2 icfg status register
 *   to determine current interrupt nesting and masking state.
 */
volatile unsigned long jiffies = 0;
volatile int scheduler_started = 0;

int cpu_in_irq(void)
{
    int flag;
    __asm__ volatile("%0 = icfg" : "=r"(flag));
    return flag & 0xff;
}

int cpu_irq_disabled(void)
{
    int flag;
    __asm__ volatile("%0 = icfg" : "=r"(flag));
    return (flag & 0x300) == 0;
}

/* -------------------------------------------------------------------------
 * 9. PI32v2 Low-Level Interrupt Control Primitives
 * -------------------------------------------------------------------------
 * Reason to keep:
 * Standard JieLi interrupt enable/disable wrappers referenced across drivers
 * and OS primitives, utilizing the PI32v2 cli and sti assembly instructions.
 *
 * Note on FreeRTOS & Multitasking:
 * Critical section nesting is managed per-task by FreeRTOS (uxCriticalNesting
 * and task context PSR/icfg). Hardware bus locking (lockset / lockclr) is
 * handled directly inside vendor FreeRTOS port assembly (port_asm.S / port.c)
 * during context switches.
 */
void local_irq_disable(void)       { __builtin_pi32v2_cli(); }
void local_irq_enable(void)        { __builtin_pi32v2_sti(); }
void __local_irq_disable(void)     { __builtin_pi32v2_cli(); }
void __local_irq_enable(void)      { __builtin_pi32v2_sti(); }
void sys_local_irq_disable(void)   { __builtin_pi32v2_cli(); }
void sys_local_irq_enable(void)    { __builtin_pi32v2_sti(); }

/* -------------------------------------------------------------------------
 * 10. FreeRTOS Application Hooks & Hardware Diagnostic Traps
 * -------------------------------------------------------------------------
 * Reason to keep:
 * - vApplicationIdleHook: Referenced by vendor tasks.c.o. Clears the hardware
 *   watchdog and puts the core to sleep until the next interrupt.
 * - vApplicationMallocFailedHook & vApplicationStackOverflowHook: Diagnostic
 *   traps that paint the ST7789 LCD full red/blue on fatal kernel errors.
 */
extern void st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

void vApplicationIdleHook(void)
{
    wdt_clear();
    __asm__ volatile("idle");
}

void vApplicationMallocFailedHook(void)
{
    st7789_fill_rect(0, 0, 240, 240, 0xF800); /* FULL RED on malloc fail */
    while (1) {
        /* Halt on heap exhaustion */
    }
}

void vApplicationStackOverflowHook(void *pxTask, char *pcTaskName)
{
    (void)pxTask;
    (void)pcTaskName;
    st7789_fill_rect(0, 0, 240, 240, 0x001F); /* FULL BLUE on stack overflow */
    while (1) {
        /* Halt on task stack overflow */
    }
}

/* -------------------------------------------------------------------------
 * 11. C++ Runtime Unwind Stubs
 * -------------------------------------------------------------------------
 */
void _Unwind_Resume(void *exception)
{
    (void)exception;
    while (1) {
        /* Exception unwinding not supported */
    }
}
