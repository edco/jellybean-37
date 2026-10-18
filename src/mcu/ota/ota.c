/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file ota.c
 * Stage 2 USB-MIDI RAM Loader for SMK-37 Pro.
 *
 * Runs entirely in SRAM at 0x01C0A800 after being loaded by SPL (uboot.boot).
 * Polls USB-MIDI, pulls application flash chunks via M-VAVE 0x30 requests,
 * writes SPI NOR flash directly (XIP and interrupts disabled), verifies CRC,
 * and reboots into the newly written firmware.
 */

#include "ota_sys.h"
#include "ota_time.h"
#include "ota_flash.h"
#include "ota_usb_hw.h"

/* Include compilation units into single translation unit for optimal size */
#include "ota_usb.c"
#include "ota_protocol.c"
#include "ota_core.c"

void ota_main(void)
{
    /* Execute OTA core flow */
    ota_core_run();
}
