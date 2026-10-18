/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_usb_desc.h
 * USB Descriptors for SMK-37 Pro Composite Audio & MIDI Device
 */

#ifndef MCU_USB_DESC_H
#define MCU_USB_DESC_H

#include <stdint.h>
#include <stddef.h>
#include "driver/device/usb/ch9.h"

#ifdef __cplusplus
extern "C" {
#endif

/* USB Endpoint Addresses matching stock AC791N mapping + CDC */
#define USB_EP0                         0x00
#define USB_EP1_CDC_DATA_OUT            0x01    /* Bulk Host->Device (CDC Data RX) */
#define USB_EP1_CDC_DATA_IN             0x81    /* Bulk Device->Host (CDC Data TX) */
#define USB_EP2_CDC_NOTIFY              0x82    /* Interrupt Device->Host (CDC Notification) */
#define USB_EP2_AUDIO_OUT               0x02    /* Isochronous Host->Device (optional/unused) */
#define USB_EP3_AUDIO_IN                0x83    /* Isochronous Device->Host (Synth Audio Out) */
#define USB_EP4_MIDI_OUT                0x04    /* Bulk Host->Device (MIDI In) */
#define USB_EP4_MIDI_IN                 0x84    /* Bulk Device->Host (MIDI Out) */

#define USB_MIDI_EP_SIZE                64
#define USB_AUDIO_EP_SIZE               288     /* 48kHz * 2 ch * 2 bytes / 1000 = 192 bytes, max 288 */
#define USB_CDC_DATA_EP_SIZE            64
#define USB_CDC_NOTIFY_EP_SIZE          16

#define USB_VID_SMK37                   0x4353  /* "CS" / SinCo-Tech */
#define USB_PID_SMK37                   0x4B4D  /* "KM" / SMK */

/* String Descriptor Indices */
#define USB_STR_IDX_LANGUAGE            0x00
#define USB_STR_IDX_MANUFACTURER        0x01
#define USB_STR_IDX_PRODUCT             0x02
#define USB_STR_IDX_SERIAL              0x03
#define USB_STR_IDX_AUDIO_ITF           0x08
#define USB_STR_IDX_MIDI_ITF            0x09
#define USB_STR_IDX_CDC_ITF             0x0A
#define USB_STR_IDX_PRIVATE             0x20
#define USB_STR_IDX_MASTER              0x21

/* Exported descriptors */
extern const struct usb_device_descriptor USB_DEVICE_DESC;
extern const uint8_t USB_CONFIG_DESC[];
extern const uint16_t USB_CONFIG_DESC_SIZE;

#define g_usb_device_desc USB_DEVICE_DESC
#define g_usb_config_desc USB_CONFIG_DESC
#define g_usb_config_desc_size USB_CONFIG_DESC_SIZE

/**
 * Retrieve string descriptor by index.
 * @param index String index requested by host
 * @param[out] out_len Length of returned string descriptor in bytes
 * @return Pointer to Unicode UTF-16LE string descriptor, or NULL if invalid.
 */
const uint8_t *mcu_usb_get_string_desc(uint8_t index, uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* MCU_USB_DESC_H */
