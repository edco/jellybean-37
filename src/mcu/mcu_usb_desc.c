/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_usb_desc.c
 * USB Descriptors for SMK-37 Pro Composite Audio & MIDI Device
 */

#include "mcu_usb_desc.h"
#include "driver/device/usb/ch9.h"
#include "driver/device/usb/device/uac_audio.h"

/* Standard 18-byte USB Device Descriptor configured for IAD Composite Device */
const struct usb_device_descriptor USB_DEVICE_DESC = {
    .bLength            = USB_DT_DEVICE_SIZE,
    .bDescriptorType    = USB_DT_DEVICE,
    .bcdUSB             = 0x0200,
#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
    .bDeviceClass       = USB_CLASS_MISC,
    .bDeviceSubClass    = 0x02,
    .bDeviceProtocol    = 0x01,
#else
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
#endif
    .bMaxPacketSize0    = 64,
    .idVendor           = USB_VID_SMK37,
    .idProduct          = USB_PID_SMK37,
    .bcdDevice          = 0x0100,
    .iManufacturer      = USB_STR_IDX_MANUFACTURER,
    .iProduct           = USB_STR_IDX_PRODUCT,
    .iSerialNumber      = USB_STR_IDX_SERIAL,
    .bNumConfigurations = 1,
};

/* Full Composite Configuration Descriptor:
 * Interface 0: Audio Control (AC)
 * Interface 1: Audio Streaming (AS) IN (Synth Audio Output to Host)
 * Interface 2: MIDI Streaming (MS) IN/OUT
 * Interface 3: CDC Communication (CCI) / Serial Notification
 * Interface 4: CDC Data (DCI) / Serial Bulk IN/OUT
 */
const uint8_t USB_CONFIG_DESC[] = {
    /* --- Configuration Header (9 bytes) --- */
    USB_DT_CONFIG_SIZE, /* bLength */
    USB_DT_CONFIG,      /* bDescriptorType: CONFIGURATION */
#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
    /* wTotalLength matching sizeof(USB_CONFIG_DESC): 250 bytes */
    0xFA, 0x00, /* wTotalLength: 250 bytes */
    0x05,       /* bNumInterfaces: 5 (AC, AS-IN, MS, CDC-CCI, CDC-DCI) */
#else
    /* wTotalLength: 184 bytes (Audio + MIDI only, 250 - 66 bytes CDC) */
    0xB8, 0x00, /* wTotalLength: 184 bytes */
    0x03,       /* bNumInterfaces: 3 (AC, AS-IN, MS) */
#endif
    0x01,       /* bConfigurationValue: 1 */
    0x00,       /* iConfiguration: 0 */
    USB_CONFIG_ATT_ONE, /* bmAttributes: Bus powered */
    0x32,       /* bMaxPower: 100 mA (50 * 2mA) */

    /* =====================================================================
     * Interface 0: Audio Control (AC)
     * ===================================================================== */
    /* Standard AC Interface Descriptor (9 bytes) */
    USB_DT_INTERFACE_SIZE, /* bLength */
    USB_DT_INTERFACE,      /* bDescriptorType: INTERFACE */
    0x00,                  /* bInterfaceNumber: 0 */
    0x00,                  /* bAlternateSetting: 0 */
    0x00,                  /* bNumEndpoints: 0 */
    USB_CLASS_AUDIO,       /* bInterfaceClass: AUDIO (0x01) */
    USB_SUBCLASS_AUDIOCONTROL, /* bInterfaceSubClass: AUDIOCONTROL (0x01) */
    0x00,                  /* bInterfaceProtocol: 0 */
    USB_STR_IDX_AUDIO_ITF, /* iInterface: "SMK-37 Pro Audio" */

    /* Class-Specific AC Interface Header (9 bytes) */
    0x09,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    UAC_HEADER,            /* bDescriptorSubtype: HEADER */
    0x00, 0x01,            /* bcdADC: Audio Class 1.00 */
    0x28, 0x00,            /* wTotalLength: 40 bytes (Header + IT + FU + OT) */
    0x01,                  /* bInCollection: 1 streaming interface */
    0x01,                  /* baInterfaceNr(1): Interface 1 belongs to this AC */

    /* Input Terminal Descriptor: Internal Synthesizer Audio Source (12 bytes) */
    0x0C,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    UAC_INPUT_TERMINAL,    /* bDescriptorSubtype: INPUT_TERMINAL */
    0x01,                  /* bTerminalID: 1 */
    0x01, 0x02,            /* wTerminalType: 0x0201 (Microphone / Line In audio source) */
    0x00,                  /* bAssocTerminal: None */
    0x02,                  /* bNrChannels: 2 (Stereo) */
    0x03, 0x00,            /* wChannelConfig: Left Front, Right Front */
    0x00,                  /* iChannelNames: None */
    0x00,                  /* iTerminal: None */

    /* Feature Unit Descriptor: Volume & Mute Control (10 bytes) */
    0x0A,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    UAC_FEATURE_UNIT,      /* bDescriptorSubtype: FEATURE_UNIT */
    0x02,                  /* bUnitID: 2 */
    0x01,                  /* bSourceID: 1 (from Input Terminal 1) */
    0x01,                  /* bControlSize: 1 byte per channel */
    0x01,                  /* bmaControls(0): Master Mute */
    0x02,                  /* bmaControls(1): Channel 1 Volume */
    0x02,                  /* bmaControls(2): Channel 2 Volume */
    0x00,                  /* iFeature: None */

    /* Output Terminal Descriptor: USB Streaming Terminal (9 bytes) */
    0x09,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    UAC_OUTPUT_TERMINAL,   /* bDescriptorSubtype: OUTPUT_TERMINAL */
    0x03,                  /* bTerminalID: 3 */
    0x01, 0x01,            /* wTerminalType: 0x0101 (USB Streaming) */
    0x00,                  /* bAssocTerminal: None */
    0x02,                  /* bSourceID: 2 (from Feature Unit 2) */
    0x00,                  /* iTerminal: None */

    /* =====================================================================
     * Interface 1: Audio Streaming (AS) IN (Synth Audio Output to Host)
     * ===================================================================== */
    /* Standard AS Interface Descriptor - Alternate 0: Zero Bandwidth (9 bytes) */
    USB_DT_INTERFACE_SIZE, /* bLength */
    USB_DT_INTERFACE,      /* bDescriptorType: INTERFACE */
    0x01,                  /* bInterfaceNumber: 1 */
    0x00,                  /* bAlternateSetting: 0 */
    0x00,                  /* bNumEndpoints: 0 */
    USB_CLASS_AUDIO,       /* bInterfaceClass: AUDIO */
    USB_SUBCLASS_AUDIOSTREAMING, /* bInterfaceSubClass: AUDIOSTREAMING */
    0x00,                  /* bInterfaceProtocol */
    0x00,                  /* iInterface */

    /* Standard AS Interface Descriptor - Alternate 1: Active Streaming (9 bytes) */
    USB_DT_INTERFACE_SIZE, /* bLength */
    USB_DT_INTERFACE,      /* bDescriptorType: INTERFACE */
    0x01,                  /* bInterfaceNumber: 1 */
    0x01,                  /* bAlternateSetting: 1 */
    0x01,                  /* bNumEndpoints: 1 (Isochronous IN) */
    USB_CLASS_AUDIO,       /* bInterfaceClass: AUDIO */
    USB_SUBCLASS_AUDIOSTREAMING, /* bInterfaceSubClass: AUDIOSTREAMING */
    0x00,                  /* bInterfaceProtocol */
    0x00,                  /* iInterface */

    /* Class-Specific AS Interface General Descriptor (7 bytes) */
    0x07,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    UAC_AS_GENERAL,        /* bDescriptorSubtype: AS_GENERAL */
    0x03,                  /* bTerminalLink: Connected to Output Terminal 3 */
    0x01,                  /* bDelay: 1 frame */
    0x01, 0x00,            /* wFormatTag: 0x0001 (PCM) */

    /* Type I Format Type Descriptor: 16-bit 48 kHz Stereo (11 bytes) */
    0x0B,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    UAC_FORMAT_TYPE,       /* bDescriptorSubtype: FORMAT_TYPE */
    0x01,                  /* bFormatType: FORMAT_TYPE_I */
    0x02,                  /* bNrChannels: 2 */
    0x02,                  /* bSubFrameSize: 2 bytes per sample (16-bit) */
    0x10,                  /* bBitResolution: 16 bits */
    0x01,                  /* bSamFreqType: 1 discrete sample frequency */
    0x80, 0xBB, 0x00,      /* tSamFreq: 48,000 Hz (0x00BB80) */

    /* Standard Isochronous Audio IN Endpoint Descriptor (EP3 IN 0x83) (9 bytes) */
    USB_DT_ENDPOINT_AUDIO_SIZE, /* bLength */
    USB_DT_ENDPOINT,       /* bDescriptorType: ENDPOINT */
    USB_EP3_AUDIO_IN,      /* bEndpointAddress: 0x83 */
    0x05,                  /* bmAttributes: Isochronous, Asynchronous data */
    0x20, 0x01,            /* wMaxPacketSize: 288 bytes (max 72 frames * 4 bytes/frame) */
    0x01,                  /* bInterval: 1 ms */
    0x00,                  /* bRefresh */
    0x00,                  /* bSynchAddress */

    /* Class-Specific Isochronous Audio Data Endpoint Descriptor (7 bytes) */
    0x07,                  /* bLength */
    USB_DT_CS_ENDPOINT,    /* bDescriptorType: CS_ENDPOINT */
    UAC_EP_GENERAL,        /* bDescriptorSubtype: EP_GENERAL */
    0x01,                  /* bmAttributes: Sampling frequency control supported */
    0x01,                  /* bLockDelayUnits: Milliseconds */
    0x01, 0x00,            /* wLockDelay: 1 ms */

    /* =====================================================================
     * Interface 2: MIDI Streaming (MS)
     * ===================================================================== */
    /* Standard MS Interface Descriptor (9 bytes) */
    USB_DT_INTERFACE_SIZE, /* bLength */
    USB_DT_INTERFACE,      /* bDescriptorType: INTERFACE */
    0x02,                  /* bInterfaceNumber: 2 */
    0x00,                  /* bAlternateSetting: 0 */
    0x02,                  /* bNumEndpoints: 2 (Bulk IN, Bulk OUT) */
    USB_CLASS_AUDIO,       /* bInterfaceClass: AUDIO */
    USB_SUBCLASS_MIDISTREAMING, /* bInterfaceSubClass: MIDISTREAMING */
    0x00,                  /* bInterfaceProtocol */
    USB_STR_IDX_MIDI_ITF,  /* iInterface: "SMK-37 Pro Midi" */

    /* Class-Specific MS Interface Header (7 bytes) */
    0x07,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    0x01,                  /* bDescriptorSubtype: MS_HEADER */
    0x00, 0x01,            /* bcdMSC: 1.00 */
    0x41, 0x00,            /* wTotalLength: 65 bytes of MS descriptors */

    /* MIDI IN Jack Descriptor - Embedded (Jack ID 1) (6 bytes) */
    0x06,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    0x02,                  /* bDescriptorSubtype: MIDI_IN_JACK */
    0x01,                  /* bJackType: EMBEDDED */
    0x01,                  /* bJackID: 1 */
    0x00,                  /* iJack: None */

    /* MIDI IN Jack Descriptor - External (Jack ID 2) (6 bytes) */
    0x06,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    0x02,                  /* bDescriptorSubtype: MIDI_IN_JACK */
    0x02,                  /* bJackType: EXTERNAL */
    0x02,                  /* bJackID: 2 */
    0x00,                  /* iJack: None */

    /* MIDI OUT Jack Descriptor - Embedded (Jack ID 3) (9 bytes) */
    0x09,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    0x03,                  /* bDescriptorSubtype: MIDI_OUT_JACK */
    0x01,                  /* bJackType: EMBEDDED */
    0x03,                  /* bJackID: 3 */
    0x01,                  /* bNrInputPins: 1 */
    0x02,                  /* baSourceID(1): External Jack 2 */
    0x01,                  /* baSourcePin(1): Pin 1 */
    0x00,                  /* iJack: None */

    /* MIDI OUT Jack Descriptor - External (Jack ID 4) (9 bytes) */
    0x09,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    0x03,                  /* bDescriptorSubtype: MIDI_OUT_JACK */
    0x02,                  /* bJackType: EXTERNAL */
    0x04,                  /* bJackID: 4 */
    0x01,                  /* bNrInputPins: 1 */
    0x01,                  /* baSourceID(1): Embedded Jack 1 */
    0x01,                  /* baSourcePin(1): Pin 1 */
    0x00,                  /* iJack: None */

    /* Standard Bulk MIDI OUT Endpoint Descriptor (EP4 OUT 0x04) (9 bytes) */
    USB_DT_ENDPOINT_AUDIO_SIZE, /* bLength */
    USB_DT_ENDPOINT,       /* bDescriptorType: ENDPOINT */
    USB_EP4_MIDI_OUT,      /* bEndpointAddress: 0x04 */
    USB_ENDPOINT_XFER_BULK, /* bmAttributes: Bulk */
    USB_MIDI_EP_SIZE, 0x00, /* wMaxPacketSize: 64 bytes */
    0x00,                  /* bInterval: Ignored for bulk */
    0x00,                  /* bRefresh */
    0x00,                  /* bSynchAddress */

    /* Class-Specific MS Bulk MIDI OUT Endpoint Descriptor (5 bytes) */
    0x05,                  /* bLength */
    USB_DT_CS_ENDPOINT,    /* bDescriptorType: CS_ENDPOINT */
    0x01,                  /* bDescriptorSubtype: MS_GENERAL */
    0x01,                  /* bNumEmbMIDIJack: 1 */
    0x01,                  /* baAssocJackID(1): Embedded Jack 1 */

    /* Standard Bulk MIDI IN Endpoint Descriptor (EP4 IN 0x84) (9 bytes) */
    USB_DT_ENDPOINT_AUDIO_SIZE, /* bLength */
    USB_DT_ENDPOINT,       /* bDescriptorType: ENDPOINT */
    USB_EP4_MIDI_IN,       /* bEndpointAddress: 0x84 */
    USB_ENDPOINT_XFER_BULK, /* bmAttributes: Bulk */
    USB_MIDI_EP_SIZE, 0x00, /* wMaxPacketSize: 64 bytes */
    0x00,                  /* bInterval: Ignored for bulk */
    0x00,                  /* bRefresh */
    0x00,                  /* bSynchAddress */

    /* Class-Specific MS Bulk MIDI IN Endpoint Descriptor (5 bytes) */
    0x05,                  /* bLength */
    USB_DT_CS_ENDPOINT,    /* bDescriptorType: CS_ENDPOINT */
    0x01,                  /* bDescriptorSubtype: MS_GENERAL */
    0x01,                  /* bNumEmbMIDIJack: 1 */
    0x03,                  /* baAssocJackID(1): Embedded Jack 3 */

#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
    /* =====================================================================
     * Interface Association Descriptor: CDC-ACM Virtual COM Port (8 bytes)
     * ===================================================================== */
    USB_DT_INTERFACE_ASSOCIATION_SIZE, /* bLength */
    USB_DT_INTERFACE_ASSOCIATION,      /* bDescriptorType: INTERFACE_ASSOCIATION */
    0x03,                  /* bFirstInterface: Interface 3 */
    0x02,                  /* bInterfaceCount: 2 interfaces (3 and 4) */
    USB_CLASS_COMM,        /* bFunctionClass: Communication and CDC Control (0x02) */
    0x02,                  /* bFunctionSubClass: Abstract Control Model (ACM) (0x02) */
    0x01,                  /* bFunctionProtocol: AT Commands (V.250) (0x01) */
    USB_STR_IDX_CDC_ITF,   /* iFunction: "SMK-37 Pro Serial" */

    /* =====================================================================
     * Interface 3: CDC Communication Interface (CCI) (9 bytes)
     * ===================================================================== */
    USB_DT_INTERFACE_SIZE, /* bLength */
    USB_DT_INTERFACE,      /* bDescriptorType: INTERFACE */
    0x03,                  /* bInterfaceNumber: 3 */
    0x00,                  /* bAlternateSetting: 0 */
    0x01,                  /* bNumEndpoints: 1 (Notification Interrupt IN) */
    USB_CLASS_COMM,        /* bInterfaceClass: CDC Control (0x02) */
    0x02,                  /* bInterfaceSubClass: Abstract Control Model (0x02) */
    0x01,                  /* bInterfaceProtocol: AT Commands (0x01) */
    USB_STR_IDX_CDC_ITF,   /* iInterface: "SMK-37 Pro Serial" */

    /* Header Functional Descriptor (5 bytes) */
    0x05,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    0x00,                  /* bDescriptorSubtype: Header Functional Descriptor */
    0x10, 0x01,            /* bcdCDC: CDC 1.10 */

    /* Call Management Functional Descriptor (5 bytes) */
    0x05,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    0x01,                  /* bDescriptorSubtype: Call Management Functional Descriptor */
    0x00,                  /* bmCapabilities: Device does not handle call management itself */
    0x04,                  /* bDataInterface: Data class interface 4 */

    /* Abstract Control Management (ACM) Functional Descriptor (4 bytes) */
    0x04,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    0x02,                  /* bDescriptorSubtype: Abstract Control Management */
    0x02,                  /* bmCapabilities: Supports line coding & state requests */

    /* Union Functional Descriptor (5 bytes) */
    0x05,                  /* bLength */
    USB_DT_CS_INTERFACE,   /* bDescriptorType: CS_INTERFACE */
    0x06,                  /* bDescriptorSubtype: Union Functional Descriptor */
    0x03,                  /* bControlInterface: Master interface 3 */
    0x04,                  /* bSubordinateInterface[0]: Slave interface 4 */

    /* Notification Endpoint Descriptor (EP2 IN 0x82) (7 bytes) */
    USB_DT_ENDPOINT_SIZE,  /* bLength */
    USB_DT_ENDPOINT,       /* bDescriptorType: ENDPOINT */
    USB_EP2_CDC_NOTIFY,    /* bEndpointAddress: 0x82 */
    USB_ENDPOINT_XFER_INT, /* bmAttributes: Interrupt */
    USB_CDC_NOTIFY_EP_SIZE, 0x00, /* wMaxPacketSize: 16 bytes */
    0x10,                  /* bInterval: 16 ms polling */

    /* =====================================================================
     * Interface 4: CDC Data Interface (DCI) (9 bytes)
     * ===================================================================== */
    USB_DT_INTERFACE_SIZE, /* bLength */
    USB_DT_INTERFACE,      /* bDescriptorType: INTERFACE */
    0x04,                  /* bInterfaceNumber: 4 */
    0x00,                  /* bAlternateSetting: 0 */
    0x02,                  /* bNumEndpoints: 2 (Bulk OUT, Bulk IN) */
    USB_CLASS_CDC_DATA,    /* bInterfaceClass: CDC Data (0x0A) */
    0x00,                  /* bInterfaceSubClass: 0 */
    0x00,                  /* bInterfaceProtocol: 0 */
    0x00,                  /* iInterface: None */

    /* Data Endpoint OUT Descriptor (EP1 OUT 0x01) (7 bytes) */
    USB_DT_ENDPOINT_SIZE,  /* bLength */
    USB_DT_ENDPOINT,       /* bDescriptorType: ENDPOINT */
    USB_EP1_CDC_DATA_OUT,  /* bEndpointAddress: 0x01 */
    USB_ENDPOINT_XFER_BULK, /* bmAttributes: Bulk */
    USB_CDC_DATA_EP_SIZE, 0x00, /* wMaxPacketSize: 64 bytes */
    0x00,                  /* bInterval: Ignored for Bulk */

    /* Data Endpoint IN Descriptor (EP1 IN 0x81) (7 bytes) */
    USB_DT_ENDPOINT_SIZE,  /* bLength */
    USB_DT_ENDPOINT,       /* bDescriptorType: ENDPOINT */
    USB_EP1_CDC_DATA_IN,   /* bEndpointAddress: 0x81 */
    USB_ENDPOINT_XFER_BULK, /* bmAttributes: Bulk */
    USB_CDC_DATA_EP_SIZE, 0x00, /* wMaxPacketSize: 64 bytes */
    0x00                   /* bInterval: Ignored for Bulk */
#endif
};

const uint16_t USB_CONFIG_DESC_SIZE = sizeof(USB_CONFIG_DESC);

/* Unicode UTF-16LE String Descriptors matching stock firmware */
static const uint8_t STR_LANGUAGE[] = {
    0x04, 0x03, 0x09, 0x04 /* English (US) 0x0409 */
};

static const uint8_t STR_MANUFACTURER[] = {
    14, 0x03,
    'M', 0, '-', 0, 'V', 0, 'A', 0, 'V', 0, 'E', 0
};

static const uint8_t STR_PRODUCT[] = {
    22, 0x03,
    'S', 0, 'M', 0, 'K', 0, '-', 0, '3', 0, '7', 0, ' ', 0, 'P', 0, 'r', 0, 'o', 0
};

static const uint8_t STR_SERIAL[] = {
    26, 0x03,
    '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '1', 0
};

/* String 8: "SMK-37 Pro Audio" (matching stock 0x020581E9) */
static const uint8_t STR_AUDIO_ITF[] = {
    34, 0x03,
    'S', 0, 'M', 0, 'K', 0, '-', 0, '3', 0, '7', 0, ' ', 0,
    'P', 0, 'r', 0, 'o', 0, ' ', 0,
    'A', 0, 'u', 0, 'd', 0, 'i', 0, 'o', 0
};

/* String 9: "SMK-37 Pro Midi" (matching stock 0x020580C6) */
static const uint8_t STR_MIDI_ITF[] = {
    32, 0x03,
    'S', 0, 'M', 0, 'K', 0, '-', 0, '3', 0, '7', 0, ' ', 0,
    'P', 0, 'r', 0, 'o', 0, ' ', 0,
    'M', 0, 'i', 0, 'd', 0, 'i', 0
};

/* String 0x20: "SMK-37 Pro-Private" (matching stock 0x0205829C) */
static const uint8_t STR_PRIVATE[] = {
    38, 0x03,
    'S', 0, 'M', 0, 'K', 0, '-', 0, '3', 0, '7', 0, ' ', 0,
    'P', 0, 'r', 0, 'o', 0, '-', 0,
    'P', 0, 'r', 0, 'i', 0, 'v', 0, 'a', 0, 't', 0, 'e', 0
};

/* String 0x21: "SMK-37 Pro-Master" (matching stock 0x02058254) */
static const uint8_t STR_MASTER[] = {
    36, 0x03,
    'S', 0, 'M', 0, 'K', 0, '-', 0, '3', 0, '7', 0, ' ', 0,
    'P', 0, 'r', 0, 'o', 0, '-', 0,
    'M', 0, 'a', 0, 's', 0, 't', 0, 'e', 0, 'r', 0
};

#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
/* String 0x0A: "SMK-37 Pro Serial" */
static const uint8_t STR_CDC_ITF[] = {
    36, 0x03,
    'S', 0, 'M', 0, 'K', 0, '-', 0, '3', 0, '7', 0, ' ', 0,
    'P', 0, 'r', 0, 'o', 0, ' ', 0,
    'S', 0, 'e', 0, 'r', 0, 'i', 0, 'a', 0, 'l', 0
};
#endif

const uint8_t *mcu_usb_get_string_desc(uint8_t index, uint16_t *out_len)
{
    const uint8_t *ptr = NULL;
    switch (index) {
    case USB_STR_IDX_LANGUAGE:
        ptr = STR_LANGUAGE;
        break;
    case USB_STR_IDX_MANUFACTURER:
        ptr = STR_MANUFACTURER;
        break;
    case USB_STR_IDX_PRODUCT:
        ptr = STR_PRODUCT;
        break;
    case USB_STR_IDX_SERIAL:
        ptr = STR_SERIAL;
        break;
    case USB_STR_IDX_AUDIO_ITF:
        ptr = STR_AUDIO_ITF;
        break;
    case USB_STR_IDX_MIDI_ITF:
        ptr = STR_MIDI_ITF;
        break;
#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
    case USB_STR_IDX_CDC_ITF:
        ptr = STR_CDC_ITF;
        break;
#endif
    case USB_STR_IDX_PRIVATE:
        ptr = STR_PRIVATE;
        break;
    case USB_STR_IDX_MASTER:
        ptr = STR_MASTER;
        break;
    default:
        return NULL;
    }

    if (ptr && out_len) {
        *out_len = ptr[0]; /* First byte is descriptor length */
    }
    return ptr;
}
