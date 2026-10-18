/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "ota_sys.h"
#include "ota_time.h"

extern void ota_usb_init(void);
extern void ota_usb_poll(void);
extern bool ota_usb_is_configured(void);
extern void flash_init_hardware(void);
extern bool ota_poll_handshake(void);
extern int ota_run_flashing_loop(void);
extern void ota_cleanup_and_reboot(void);
extern void ota_usb_detach(void);

void ota_core_run(void)
{
    /* 0. Disable hardware watchdog to prevent unexpected reset during OTA */
    wdt_close();

    /* 1. Halt Core 1 execution */
    corex2(0)->C1_CON |= (1u << 1);
    corex2(0)->C1_CON &= ~(1u << 3);
    sys_sync();

    /* 2. Initialize 24 MHz hardware time base */
    time_init();

    /* 3. Initialize USB-MIDI hardware */
    ota_usb_init();

    /* 4. Service USB and wait for host handshake (HS_QUERY / UPGRADE) */
    uint32_t start_ms = time_get_ms();
    bool host_started = false;
    while ((time_get_ms() - start_ms) < 45000) {
        wdt_clear();
        ota_usb_poll();
        if (ota_poll_handshake()) {
            host_started = true;
            break;
        }
    }

    /* 5. Enter Stage 2 OTA pull-flashing loop ONLY if host handshake started */
    if (host_started) {
        flash_init_hardware();
        time_delay_ms(100);
        ota_usb_poll();
        ota_run_flashing_loop();
    }

    /* 6. Clean fallback: clear update flags and staging partition so device reboots normally */
    flash_init_hardware();
    ota_cleanup_and_reboot();
}
