/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef OTA_USB_HW_H
#define OTA_USB_HW_H

#include "ota_sys.h"
#include "asm/usb.h"
#include "driver/device/usb/usb_phy.h"
#include "driver/device/usb/ch9.h"

/* USB_CON0 bit 4 (Test Mode 2 / CID) */
#ifndef TM2
#define TM2 4u
#endif

#endif /* OTA_USB_HW_H */
