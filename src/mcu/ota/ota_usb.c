/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "ota_usb_hw.h"
#include "ota_time.h"

/* USB Device Descriptor (SMK-37 Pro USB-MIDI Identity) */
static const struct usb_device_descriptor DEVICE_DESC = {
    .bLength            = USB_DT_DEVICE_SIZE,
    .bDescriptorType    = USB_DT_DEVICE,
    .bcdUSB             = 0x0200, /* USB 2.0 (little-endian wire: 0x00, 0x02) */
    .bDeviceClass       = USB_CLASS_PER_INTERFACE,
    .bDeviceSubClass    = 0,
    .bDeviceProtocol    = 0,
    .bMaxPacketSize0    = 64,
    .idVendor           = 0x4353, /* "CS" / SinCo-Tech (0x53, 0x43 on wire) */
    .idProduct          = 0xCF4D, /* "M\xCF" / SMK-37 Pro Midi (0x4D, 0xCF on wire) */
    .bcdDevice          = 0x0100, /* v1.00 */
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 0,
    .bNumConfigurations = 1,
};

/* USB Configuration Descriptor (AudioControl IF 0, MIDIStreaming IF 1, EP1) */
static const uint8_t CONFIG_DESC[] = {
    /* Configuration Descriptor */
    USB_DT_CONFIG_SIZE, USB_DT_CONFIG,
    101, 0,             /* wTotalLength = 101 bytes */
    2,                  /* bNumInterfaces = 2 */
    1,                  /* bConfigurationValue */
    0,                  /* iConfiguration */
    USB_CONFIG_ATT_ONE, /* bmAttributes (Bus Powered) */
    50,                 /* bMaxPower (100 mA) */

    /* Standard AC Interface Descriptor (Interface 0) */
    USB_DT_INTERFACE_SIZE, USB_DT_INTERFACE, 0, 0, 0, USB_CLASS_AUDIO, 0x01, 0x00, 0,

    /* Class-specific AC Interface Descriptor (HEADER) */
    9, USB_DT_CS_INTERFACE, 0x01, 0x00, 0x01, 9, 0, 1, 1,

    /* Standard MS Interface Descriptor (Interface 1) */
    USB_DT_INTERFACE_SIZE, USB_DT_INTERFACE, 1, 0, 2, USB_CLASS_AUDIO, 0x03, 0x00, 0,

    /* Class-specific MS Interface Header */
    7, USB_DT_CS_INTERFACE, 0x01, 0x00, 0x01, 65, 0,

    /* MIDI IN Jack (Embedded) */
    6, USB_DT_CS_INTERFACE, 0x02, 0x01, 1, 0,

    /* MIDI IN Jack (External) */
    6, USB_DT_CS_INTERFACE, 0x02, 0x02, 2, 0,

    /* MIDI OUT Jack (Embedded) */
    9, USB_DT_CS_INTERFACE, 0x03, 0x01, 3, 1, 2, 1, 0,

    /* MIDI OUT Jack (External) */
    9, USB_DT_CS_INTERFACE, 0x03, 0x02, 4, 1, 1, 1, 0,

    /* Bulk OUT Endpoint Descriptor (EP1 OUT) */
    USB_DT_ENDPOINT_AUDIO_SIZE, USB_DT_ENDPOINT, 0x01, USB_ENDPOINT_XFER_BULK, 64, 0, 0, 0, 0,

    /* Class-specific Bulk OUT Endpoint */
    5, USB_DT_CS_ENDPOINT, 0x01, 1, 1,

    /* Bulk IN Endpoint Descriptor (EP1 IN) */
    USB_DT_ENDPOINT_AUDIO_SIZE, USB_DT_ENDPOINT, 0x81, USB_ENDPOINT_XFER_BULK, 64, 0, 0, 0, 0,

    /* Class-specific Bulk IN Endpoint */
    5, USB_DT_CS_ENDPOINT, 0x01, 1, 3
};

/* String Descriptors */
static const uint8_t STR_LANG[] = { 4, USB_DT_STRING, 0x09, 0x04 }; /* EN-US */
static const uint8_t STR_MFG[] = {
    14, USB_DT_STRING,
    'M', 0, '-', 0, 'V', 0, 'A', 0, 'V', 0, 'E', 0
};
static const uint8_t STR_PROD[] = {
    22, USB_DT_STRING,
    'S', 0, 'M', 0, 'K', 0, '-', 0, '3', 0, '7', 0, ' ', 0, 'P', 0, 'r', 0, 'o', 0
};

/* USB DMA Buffers (Aligned to 32 bytes for hardware DMA) */
static uint8_t ep0_buf[64 + 4] __attribute__((aligned(32)));
static uint8_t ep1_tx_buf[64] __attribute__((aligned(32)));
static uint8_t ep1_rx_buf[64 + 4] __attribute__((aligned(32)));

/* USB EP0 State */
static const uint8_t *ep0_tx_ptr = NULL;
static uint16_t ep0_tx_left = 0;
static uint8_t pending_addr = 0;
static bool usb_configured = false;

bool ota_usb_is_configured(void)
{
    return usb_configured;
}

/* Outgoing MIDI Buffer FIFO */
#define USB_MIDI_TX_FIFO_SIZE 256
static uint8_t midi_tx_buf[USB_MIDI_TX_FIFO_SIZE];
static uint16_t midi_tx_head = 0;
static uint16_t midi_tx_tail = 0;

void ota_usb_queue_tx_packet(uint8_t cin, uint8_t m0, uint8_t m1, uint8_t m2)
{
    uint8_t pkt[4] = { cin, m0, m1, m2 };
    for (int i = 0; i < 4; i++) {
        uint16_t next = (midi_tx_head + 1) % USB_MIDI_TX_FIFO_SIZE;
        if (next != midi_tx_tail) {
            midi_tx_buf[midi_tx_head] = pkt[i];
            midi_tx_head = next;
        }
    }
}

/* External Callback for Received MIDI SysEx Frame */
extern void ota_on_received_frame(const uint8_t *payload, uint32_t len);

static uint8_t sysex_rx_buf[768];
static uint32_t sysex_rx_len = 0;
static bool in_sysex = false;

static void usb_midi_process_byte(uint8_t b)
{
    if (b >= 0xF8) {
        return; /* Ignore Real-Time messages (clock, active sense, etc.) */
    }

    if (b == 0xF0) {
        sysex_rx_len = 0;
        in_sysex = true;
    } else if (b == 0xF7) {
        if (in_sysex && sysex_rx_len > 0) {
            ota_on_received_frame(sysex_rx_buf, sysex_rx_len);
        }
        in_sysex = false;
        sysex_rx_len = 0;
    } else if (in_sysex) {
        if (sysex_rx_len < sizeof(sysex_rx_buf)) {
            sysex_rx_buf[sysex_rx_len++] = b;
        }
    }
}

static void usb_ep0_service(void)
{
    sie_write(MUSB_INDEX, 0);
    uint32_t csr0 = sie_read(MUSB_CSR0);

    /* 1. Handle SentStall */
    if (csr0 & CSR0P_SentStall) {
        sie_write(MUSB_CSR0, 0);
        return;
    }

    /* 2. Handle SetupEnd */
    if (csr0 & CSR0P_SetupEnd) {
        sie_write(MUSB_CSR0, CSR0P_ClrSetupEnd);
        ep0_tx_ptr = NULL;
        ep0_tx_left = 0;
        pending_addr = 0;
        return;
    }

    /* 3. Service incoming SETUP packet */
    if (csr0 & CSR0P_RxPktRdy) {
        sys_sync();
        struct usb_ctrlrequest req;
        memcpy(&req, ep0_buf, sizeof(req));
        JL_USB->EP0_ADR = (uint32_t)(uintptr_t)ep0_buf;

        ep0_tx_ptr = NULL;
        ep0_tx_left = 0;

        switch (req.bRequest) {
        case USB_REQ_GET_DESCRIPTOR: {
            uint8_t desc_type = (uint8_t)(req.wValue >> 8);
            uint8_t desc_index = (uint8_t)req.wValue;

            if (desc_type == USB_DT_DEVICE) {
                ep0_tx_ptr = (const uint8_t *)&DEVICE_DESC;
                ep0_tx_left = sizeof(DEVICE_DESC);
            } else if (desc_type == USB_DT_CONFIG) {
                ep0_tx_ptr = CONFIG_DESC;
                ep0_tx_left = sizeof(CONFIG_DESC);
            } else if (desc_type == USB_DT_STRING) {
                if (desc_index == 0) {
                    ep0_tx_ptr = STR_LANG;
                    ep0_tx_left = sizeof(STR_LANG);
                } else if (desc_index == 1) {
                    ep0_tx_ptr = STR_MFG;
                    ep0_tx_left = sizeof(STR_MFG);
                } else if (desc_index == 2) {
                    ep0_tx_ptr = STR_PROD;
                    ep0_tx_left = sizeof(STR_PROD);
                } else {
                    sie_write(MUSB_CSR0, CSR0P_SendStall);
                    return;
                }
            } else {
                sie_write(MUSB_CSR0, CSR0P_SendStall);
                return;
            }
            break;
        }

        case USB_REQ_SET_ADDRESS:
            pending_addr = (uint8_t)req.wValue;
            sie_write(MUSB_CSR0, CSR0P_ClrRxPktRdy | CSR0P_DataEnd);
            return;

        case USB_REQ_CLEAR_FEATURE: { /* ENDPOINT_HALT */
            if (req.wValue == 0) {
                uint8_t ep = (uint8_t)req.wIndex & 0x0F;
                if (ep == 1) {
                    sie_write(MUSB_INDEX, 1);
                    if (req.wIndex & 0x80) {
                        sie_write(MUSB_TXCSR1, TXCSRP_ClrDataTog);
                    } else {
                        sie_write(MUSB_RXCSR1, RXCSRP_ClrDataTog);
                    }
                    sie_write(MUSB_INDEX, 0);
                }
            }
            sie_write(MUSB_CSR0, CSR0P_ClrRxPktRdy | CSR0P_DataEnd);
            return;
        }

        case USB_REQ_SET_CONFIGURATION:
            usb_configured = (req.wValue != 0);
            if (usb_configured) {
                JL_USB->EP1_TADR = (uint32_t)(uintptr_t)ep1_tx_buf;
                JL_USB->EP1_RADR = (uint32_t)(uintptr_t)ep1_rx_buf;
                sie_write(MUSB_INDEX, 1);
                sie_write(MUSB_TXMAXP, 0xFF);
                sie_write(MUSB_TXCSR1, TXCSRP_ClrDataTog | TXCSRP_FlushFIFO);
                sie_write(MUSB_TXCSR2, 0);
                sie_write(MUSB_RXMAXP, 0xFF);
                sie_write(MUSB_RXCSR1, RXCSRP_ClrDataTog | RXCSRP_FlushFIFO);
                sie_write(MUSB_RXCSR2, 0);
                sie_write(MUSB_INTRRX1E, 0x02);
                JL_USB->CON0 &= ~(1u << 20); /* Enable EP1 */
            } else {
                sie_write(MUSB_INTRRX1E, 0);
            }
            sie_write(MUSB_INDEX, 0);
            sie_write(MUSB_CSR0, CSR0P_ClrRxPktRdy | CSR0P_DataEnd);
            return;

        case USB_REQ_GET_STATUS: {
            static const uint16_t status = 0;
            ep0_tx_ptr = (const uint8_t *)&status;
            ep0_tx_left = 2;
            break;
        }

        default:
            sie_write(MUSB_CSR0, CSR0P_SendStall);
            return;
        }

        if (ep0_tx_ptr && ep0_tx_left > 0) {
            if (ep0_tx_left > req.wLength) {
                ep0_tx_left = req.wLength;
            }
            uint8_t chunk = (ep0_tx_left > 64) ? 64 : (uint8_t)ep0_tx_left;

            /* Two-phase sequence: Step 1: Clear RxPktRdy */
            sie_write(MUSB_CSR0, CSR0P_ClrRxPktRdy);

            /* Step 2: Load data payload and set EP0 count */
            memcpy(ep0_buf, ep0_tx_ptr, chunk);
            sys_sync();
            JL_USB->EP0_CNT = chunk;
            sys_sync();
            ep0_tx_ptr += chunk;
            ep0_tx_left -= chunk;

            /* Step 3: Prime TX */
            if (ep0_tx_left == 0) {
                sie_write(MUSB_CSR0, CSR0P_TxPktRdy | CSR0P_DataEnd);
                ep0_tx_ptr = NULL;
            } else {
                sie_write(MUSB_CSR0, CSR0P_TxPktRdy);
            }
        } else {
            sie_write(MUSB_CSR0, CSR0P_ClrRxPktRdy | CSR0P_DataEnd);
        }
    } else if (ep0_tx_ptr && ep0_tx_left > 0 && !(csr0 & CSR0P_TxPktRdy)) {
        /* Continue multi-packet transfer */
        uint8_t chunk = (ep0_tx_left > 64) ? 64 : (uint8_t)ep0_tx_left;
        memcpy(ep0_buf, ep0_tx_ptr, chunk);
        sys_sync();
        JL_USB->EP0_CNT = chunk;
        sys_sync();
        ep0_tx_ptr += chunk;
        ep0_tx_left -= chunk;

        if (ep0_tx_left == 0) {
            sie_write(MUSB_CSR0, CSR0P_TxPktRdy | CSR0P_DataEnd);
            ep0_tx_ptr = NULL;
        } else {
            sie_write(MUSB_CSR0, CSR0P_TxPktRdy);
        }
    } else if (pending_addr != 0) {
        /* Status stage IN completion: commit device address now */
        sie_write(MUSB_FADDR, pending_addr);
        pending_addr = 0;
    }
}

static void usb_ep1_rx(void)
{
    sie_write(MUSB_INDEX, 1);
    uint32_t rx_limit = 32;
    while (rx_limit--) {
        uint32_t csr = sie_read(MUSB_RXCSR1) | (sie_read(MUSB_RXCSR2) << 8);
        if (!(csr & RXCSRP_RxPktRdy)) {
            break;
        }

        uint32_t count = sie_read(MUSB_RXCOUNT1) | (sie_read(MUSB_RXCOUNT2) << 8);
        if (count > 64) {
            count = 64;
        }

        __asm__ volatile("ssync" ::: "memory");

        uint32_t idx = 0;
        while (idx + 4 <= count) {
            uint8_t cin = ep1_rx_buf[idx] & 0x0Fu;
            uint8_t m0 = ep1_rx_buf[idx + 1];
            uint8_t m1 = ep1_rx_buf[idx + 2];
            uint8_t m2 = ep1_rx_buf[idx + 3];
            idx += 4;

            if (cin == 0x04) {
                usb_midi_process_byte(m0);
                usb_midi_process_byte(m1);
                usb_midi_process_byte(m2);
            } else if (cin == 0x05 || cin == 0x0F) {
                usb_midi_process_byte(m0);
            } else if (cin == 0x06) {
                usb_midi_process_byte(m0);
                usb_midi_process_byte(m1);
            } else if (cin == 0x07) {
                usb_midi_process_byte(m0);
                usb_midi_process_byte(m1);
                usb_midi_process_byte(m2);
            }
        }

        /* Reset FIFO pointer, clear RxPktRdy and any overrun/stall flags */
        csr = (csr & ~0x164u) | RXCSRP_FlushFIFO;
        sie_write(MUSB_RXCSR1, csr & 0xFFu);
        sie_write(MUSB_RXCSR2, csr >> 8);
        __asm__ volatile("csync" ::: "memory");
    }
}

void ota_usb_reset_rx_state(void)
{
    in_sysex = false;
    sysex_rx_len = 0;
    sie_write(MUSB_INDEX, 1);
    uint32_t csr = sie_read(MUSB_RXCSR1) | (sie_read(MUSB_RXCSR2) << 8);
    csr = (csr & ~0x164u) | RXCSRP_FlushFIFO;
    sie_write(MUSB_RXCSR1, csr & 0xFFu);
    sie_write(MUSB_RXCSR2, csr >> 8);
}

void ota_usb_flush_tx(void)
{
    midi_tx_head = 0;
    midi_tx_tail = 0;
}

static void usb_ep1_tx(void)
{
    if (midi_tx_head == midi_tx_tail) {
        return;
    }
    sie_write(MUSB_INDEX, 1);
    uint32_t txcsr = sie_read(MUSB_TXCSR1);
    if (!(txcsr & TXCSRP_TxPktRdy)) {
        uint8_t count = 0;
        while (midi_tx_head != midi_tx_tail && count < 64) {
            ep1_tx_buf[count++] = midi_tx_buf[midi_tx_tail];
            midi_tx_tail = (midi_tx_tail + 1) % USB_MIDI_TX_FIFO_SIZE;
        }
        if (count > 0) {
            sys_sync();
            JL_USB->EP1_TADR = (uint32_t)(uintptr_t)ep1_tx_buf;
            sys_sync();
            JL_USB->EP1_CNT = count;
            sys_sync();
            sie_write(MUSB_TXCSR1, TXCSRP_TxPktRdy);
        }
    }
}

void ota_usb_init(void)
{
    /* Reset FIFO and state pointers */
    midi_tx_head = 0;
    midi_tx_tail = 0;
    sysex_rx_len = 0;
    in_sysex = false;
    usb_configured = false;
    pending_addr = 0;
    ep0_tx_ptr = NULL;
    ep0_tx_left = 0;

    /* Reset USB controller */
    JL_USB->CON0 = 0;
    JL_USB->CON1 = 0;
    JL_USB_IO->CON0 = (1u << 2) | (1u << 3); /* 0x0C */
    time_delay_ms(25);

    /* Clock source: PLL48M routed to USB */
    JL_CLOCK->CLK_CON1 &= ~3u;

    /* Pre-bind DMA buffers */
    JL_USB->EP0_ADR = (uint32_t)(uintptr_t)ep0_buf;
    JL_USB->EP1_TADR = (uint32_t)(uintptr_t)ep1_tx_buf;
    JL_USB->EP1_RADR = (uint32_t)(uintptr_t)ep1_rx_buf;

    /* Enable D+ / D- digital inputs, D+ 1.5k pull-up, and SR (Full-Speed slew rate) */
    JL_USB_IO->CON0 = (1u << SR) | (1u << DPPU) | (1u << DPDIE) | (1u << DMDIE);

    /* Power up SIE and PHY */
    JL_USB->CON0 &= ~0x4035u;
    JL_USB->CON1 = 0;
    JL_USB->CON0 |= (1u << PHY_ON);
    JL_USB->CON0 |= (1u << USB_NRST) | (1u << TM1) | (1u << TM2) | (1u << VBUS);
    JL_USB->CON0 &= ~(0x1Fu << 19); /* Enable EP0..4 */

    /* Configure SIE registers for Full-Speed mode (0x40) */
    sie_write(MUSB_POWER, 0x40);
    sie_write(MUSB_INTRUSBE, 0x07); /* Reset, resume, suspend */
    sie_write(MUSB_INTRTX1E, 0x01); /* EP0 TX interrupt enable */
    sie_write(MUSB_INTRTX2E, 0);
    sie_write(MUSB_INTRRX1E, 0x02); /* EP1 RX interrupt enable */
    sie_write(MUSB_INTRRX2E, 0);

    /* Configure EP1 FIFOs and MAXP registers */
    sie_write(MUSB_INDEX, 1);
    sie_write(MUSB_TXMAXP, 0xFF);
    sie_write(MUSB_TXCSR1, TXCSRP_ClrDataTog | TXCSRP_FlushFIFO);
    sie_write(MUSB_TXCSR2, 0);
    sie_write(MUSB_RXMAXP, 0xFF);
    sie_write(MUSB_RXCSR1, RXCSRP_ClrDataTog | RXCSRP_FlushFIFO);
    sie_write(MUSB_RXCSR2, 0);
}

void ota_usb_detach(void)
{
    JL_USB->CON0 = 0;
    JL_USB_IO->CON0 = (1u << 2) | (1u << 3);
    sys_sync();
    time_delay_ms(100);
}

void ota_usb_poll(void)
{
    if (!(JL_USB->CON0 & (1u << USB_NRST))) {
        return;
    }

    /* Check bus events */
    uint32_t iu = sie_read(MUSB_INTRUSB);
    if (iu & INTRUSB_RESET_BABBLE) { /* USB Bus Reset */
        sie_write(MUSB_FADDR, 0);
        pending_addr = 0;
        usb_configured = false;
        JL_USB->EP0_ADR = (uint32_t)(uintptr_t)ep0_buf;
        JL_USB->EP1_TADR = (uint32_t)(uintptr_t)ep1_tx_buf;
        JL_USB->EP1_RADR = (uint32_t)(uintptr_t)ep1_rx_buf;
        sie_write(MUSB_INTRUSBE, 0x07);
        sie_write(MUSB_INTRTX1E, 0x01);
        sie_write(MUSB_INTRRX1E, 0x02);
    }

    uint32_t it = sie_read(MUSB_INTRTX1);
    uint32_t ir = sie_read(MUSB_INTRRX1);

    if (it & 0x01u) {
        usb_ep0_service();
    }
    if ((ir & 0x02u) || usb_configured) {
        usb_ep1_rx();
    }
    if (usb_configured) {
        usb_ep1_tx();
    }
}
