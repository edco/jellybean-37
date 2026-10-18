/*
 * Copyright (c) 2026 Ed Coffey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/**
 * @file mcu_usb.c
 * USB Hardware Controller Driver for JieLi AC791N (WL82)
 *
 * Implements Full-Speed USB composite device (Audio Streaming IN + MIDI IN/OUT)
 * directly using the AC791N hardware registers and JieLi silicon driver.
 */

#include "asm/cpu.h"
#include "usb/usb_phy.h"
#include "asm/hwi.h"
#include "asm/usb.h"
#include "asm/WL82.h"
#include "driver/device/gpio.h"
#include "driver/device/usb/ch9.h"
#include "driver/device/usb/device/uac_audio.h"

#include "mcu_usb.h"
#include "mcu_usb_desc.h"
#include "mcu_midi.h"
#include "mcu_cdc.h"
#include "hal_midi.h"

#include <string.h>

extern void request_irq(unsigned char index, unsigned char priority, void (*handler)(void), unsigned char cpu_id);
extern void usb_slave_init(const usb_dev usb_id);
extern void usb_enable_ep(const usb_dev usb_id, u32 ep);
extern void usb_disable_ep(const usb_dev usb_id, u32 ep);

/* CDC Class-Specific Request Codes */
#define CDC_REQ_SET_LINE_CODING         0x20
#define CDC_REQ_GET_LINE_CODING         0x21
#define CDC_REQ_SET_CONTROL_LINE_STATE  0x22
#define CDC_REQ_SEND_BREAK              0x23

/* USB Endpoint Types mapped to SDK ch9 transfer types */
#define USB_EP_TYPE_CTRL            USB_ENDPOINT_XFER_CONTROL
#define USB_EP_TYPE_ISO             USB_ENDPOINT_XFER_ISOC
#define USB_EP_TYPE_BULK            USB_ENDPOINT_XFER_BULK
#define USB_EP_TYPE_INTR            USB_ENDPOINT_XFER_INT

/* Device States */
typedef enum {
    DEV_STATE_DEFAULT = 0,
    DEV_STATE_ADDRESSED,
    DEV_STATE_CONFIGURED
} UsbDevState;

static volatile UsbDevState dev_state = DEV_STATE_DEFAULT;
static volatile uint8_t dev_address = 0;
static volatile uint8_t active_config = 0;
static volatile uint8_t audio_alt_setting = 0; /* 0 = idle, 1 = active streaming */
static volatile bool ep4_in_busy = false;

#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
/* CDC Line Coding (7 bytes, packed) */
typedef struct __attribute__((packed)) {
    uint32_t dte_rate;
    uint8_t  char_format;
    uint8_t  parity_type;
    uint8_t  data_bits;
} CdcLineCoding;

static CdcLineCoding cdc_line_coding = {
    .dte_rate = 115200,
    .char_format = 0, /* 1 Stop bit */
    .parity_type = 0, /* None */
    .data_bits = 8    /* 8 Data bits */
};

static volatile bool cdc_dtr_active = false;
static volatile bool cdc_rts_active = false;
static volatile bool cdc_zlp_pending = false;
static volatile bool ep1_in_busy = false;

static uint8_t ep1_in_dma[USB_CDC_DATA_EP_SIZE] __attribute__((aligned(32)));
static uint8_t ep1_out_dma[USB_CDC_DATA_EP_SIZE] __attribute__((aligned(32)));
static uint8_t ep2_in_dma[32] __attribute__((aligned(32)));
#endif

/* Static 32-byte cache-aligned DMA Endpoint Buffers */
static uint8_t ep0_buffer[64] __attribute__((aligned(32)));
static uint8_t ep3_in_dma[USB_AUDIO_EP_SIZE] __attribute__((aligned(32)));
static uint8_t ep4_in_dma[USB_MIDI_EP_SIZE] __attribute__((aligned(32)));
static uint8_t ep4_out_dma[USB_MIDI_EP_SIZE] __attribute__((aligned(32)));
static struct usb_ep_addr_t usb_ep_addr;

/* Audio Streaming Isochronous Ring Buffer (2048 frames = ~42.6 ms of 48kHz stereo)
 * SPSC lock-free ring between Core 1 audio renderer and Core 0 USB ISR. */
#define AUDIO_RING_FRAMES       2048
#define AUDIO_RING_MASK         (AUDIO_RING_FRAMES - 1)
#define AUDIO_TARGET_BUFFER     512  /* ~10.6 ms cushion for jitter & rate matching */

static int16_t audio_ring[AUDIO_RING_FRAMES * 2] __attribute__((aligned(32)));

/* Producer (Core 1) control - separate 32-byte cache line */
static volatile uint32_t audio_ring_head __attribute__((aligned(32))) = 0;
static uint8_t pad_audio_head[28] __attribute__((unused));

/* Consumer (Core 0) control - separate 32-byte cache line */
static volatile uint32_t audio_ring_tail __attribute__((aligned(32))) = 0;
static uint8_t pad_audio_tail[28] __attribute__((unused));

static volatile bool audio_buffering = true;

#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
/* CDC SPSC Ring Buffers for Non-blocking Console/Logging Pipeline */
#define CDC_TX_RING_SIZE        2048
#define CDC_TX_RING_MASK        (CDC_TX_RING_SIZE - 1)
#define CDC_RX_RING_SIZE        512
#define CDC_RX_RING_MASK        (CDC_RX_RING_SIZE - 1)

static uint8_t cdc_tx_ring[CDC_TX_RING_SIZE];
static volatile uint16_t cdc_tx_head = 0;
static volatile uint16_t cdc_tx_tail = 0;
static volatile uint32_t cdc_dropped_bytes = 0;

static uint8_t cdc_rx_ring[CDC_RX_RING_SIZE];
static volatile uint16_t cdc_rx_head = 0;
static volatile uint16_t cdc_rx_tail = 0;
static void (*cdc_rx_callback)(uint8_t byte) = NULL;

static inline int cdc_irq_save(void)
{
    int flags, tmp;
    __asm__ volatile ("%0 = icfg" : "=r"(flags));
    tmp = flags & ~(1 << 8);
    __asm__ volatile ("icfg = %0" :: "r"(tmp));
    return flags;
}

static inline void cdc_irq_restore(int flags)
{
    __asm__ volatile ("icfg = %0" :: "r"(flags));
}
#endif

/* Active EP0 Control Transfer Context */
typedef struct {
    const uint8_t *tx_ptr;
    uint16_t tx_remaining;
    uint16_t rx_remaining;
    bool set_address_pending;
#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
    bool is_cdc_line_coding;
#endif
    uint8_t pending_address;
} Ep0CtrlCtx;

static Ep0CtrlCtx ep0_ctx;

/* -------------------------------------------------------------------------
 * Low-Level USB Peripheral & PHY Setup
 * ------------------------------------------------------------------------- */
static void mcu_usb_phy_config(void)
{
    /* Configure FUSBDP (pin 48) and FUSBDM (pin 47) for USB Device Mode:
     * D+ has 1.5k pull-up to signal Full-Speed connection to host. */
    JL_USB_IO->CON0 = (1 << DPPU) | (1 << IO_PU_MODE) | (1 << DPDIE) | (1 << DMDIE);
    __asm__ volatile("csync");
}

/* -------------------------------------------------------------------------
 * EP0 Control Transfer Handler
 * ------------------------------------------------------------------------- */
static void ep0_send_data(const uint8_t *data, uint16_t len, uint16_t max_requested)
{
    if (len > max_requested) {
        len = max_requested;
    }

    uint16_t chunk = len;
    if (chunk > 64) {
        chunk = 64;
    }

    if (chunk > 0 && data) {
        usb_ep0_ClrRxPktRdy(0);
        memcpy(ep0_buffer, data, chunk);
        __asm__ volatile("ssync; csync" ::: "memory");
        JL_USB->EP0_CNT = chunk;
        __asm__ volatile("ssync; csync" ::: "memory");
        ep0_ctx.tx_ptr = data + chunk;
        ep0_ctx.tx_remaining = len - chunk;
        if (ep0_ctx.tx_remaining == 0) {
            usb_ep0_TxPktEnd(0);
        } else {
            usb_write_csr0(0, CSR0P_TxPktRdy);
        }
    } else {
        usb_ep0_RxPktEnd(0); /* Zero-length ACK for request with no data stage */
        ep0_ctx.tx_ptr = NULL;
        ep0_ctx.tx_remaining = 0;
    }
}

static void ep0_handle_setup(const uint8_t *req)
{
    uint8_t req_type_raw = req[0];
    uint8_t req_code = req[1];
    uint16_t req_value = req[2] | ((uint16_t)req[3] << 8);
    uint16_t req_index = req[4] | ((uint16_t)req[5] << 8);
    uint16_t req_len = req[6] | ((uint16_t)req[7] << 8);

    uint8_t req_type = (req_type_raw >> 5) & 0x03;
    uint8_t recipient = req_type_raw & 0x1F;
    (void)recipient;

    if (req_type == 0) { /* Standard USB Request */
        switch (req_code) {
        case USB_REQ_GET_STATUS: {
            static const uint8_t status_zero[2] = { 0, 0 };
            ep0_send_data(status_zero, 2, req_len);
            break;
        }

        case USB_REQ_SET_ADDRESS: {
            ep0_ctx.set_address_pending = true;
            ep0_ctx.pending_address = (uint8_t)(req_value & 0x7F);
            ep0_send_data(NULL, 0, 0); /* Zero-length status ACK */
            break;
        }

        case USB_REQ_GET_DESCRIPTOR: {
            uint8_t desc_type = (uint8_t)(req_value >> 8);
            uint8_t desc_idx = (uint8_t)(req_value & 0xFF);

            if (desc_type == USB_DT_DEVICE) { /* Device Descriptor */
                ep0_send_data((const uint8_t *)&USB_DEVICE_DESC, sizeof(USB_DEVICE_DESC), req_len);
            } else if (desc_type == USB_DT_CONFIG) { /* Configuration Descriptor */
                ep0_send_data(USB_CONFIG_DESC, USB_CONFIG_DESC_SIZE, req_len);
            } else if (desc_type == USB_DT_STRING) { /* String Descriptor */
                uint16_t str_len = 0;
                const uint8_t *str_ptr = mcu_usb_get_string_desc(desc_idx, &str_len);
                if (str_ptr && str_len > 0) {
                    ep0_send_data(str_ptr, str_len, req_len);
                } else {
                    usb_ep0_Set_Stall(0);
                }
            } else {
                usb_ep0_Set_Stall(0);
            }
            break;
        }

        case USB_REQ_GET_CONFIGURATION: {
            uint8_t cfg = active_config;
            ep0_send_data(&cfg, 1, req_len);
            break;
        }

        case USB_REQ_SET_CONFIGURATION: {
            active_config = (uint8_t)req_value;
            dev_state = (req_value > 0) ? DEV_STATE_CONFIGURED : DEV_STATE_ADDRESSED;
            ep0_send_data(NULL, 0, 0);
            break;
        }

        case USB_REQ_GET_INTERFACE: {
            uint8_t alt = (req_index == 1) ? audio_alt_setting : 0;
            ep0_send_data(&alt, 1, req_len);
            break;
        }

        case USB_REQ_SET_INTERFACE: {
            if (req_index == 1) {
                /* Interface 1: Audio Streaming IN Alternate Setting */
                uint8_t alt = (uint8_t)(req_value & 0xFF);
                audio_alt_setting = alt;
                if (alt == 1) {
                    audio_ring_head = 0;
                    audio_ring_tail = 0;
                    __asm__ volatile("csync");
                    audio_buffering = true;
                    /* Enable EP3 hardware in CON0, prime initial frame, and enable TX interrupt */
                    usb_enable_ep(0, 3);
                    usb_set_intr_txe(0, 3);
                    memset(ep3_in_dma, 0, 192);
                    __asm__ volatile("ssync; csync" ::: "memory");
                    usb_g_ep_write(0, 3, ep3_in_dma, 192);
                } else {
                    usb_clr_intr_txe(0, 3);
                    usb_disable_ep(0, 3);
                }
            }
            ep0_send_data(NULL, 0, 0);
            break;
        }

        default:
            usb_ep0_Set_Stall(0);
            break;
        }
    } else if (req_type == 1) { /* Class-Specific Request (Audio / MIDI / CDC) */
        switch (req_code) {
        case UAC_GET_CUR:
        case UAC_GET_MIN:
        case UAC_GET_MAX:
        case UAC_GET_RES: {
            if (recipient == 2) {
                /* Endpoint control: Return discrete 48kHz frequency (3 bytes: 0x00BB80) */
                static const uint8_t freq_resp[3] = { 0x80, 0xBB, 0x00 };
                ep0_send_data(freq_resp, (req_len > 3) ? 3 : req_len, req_len);
            } else {
                /* Unit/Interface control: Return default volume / mute settings */
                static const uint8_t vol_resp[2] = { 0x00, 0x00 };
                ep0_send_data(vol_resp, (req_len > 2) ? 2 : req_len, req_len);
            }
            break;
        }

        case UAC_SET_CUR: {
            if (req_len > 0 && (req_type_raw & 0x80) == 0) {
                /* Prepare to receive OUT data payload from host */
                ep0_ctx.rx_remaining = req_len;
                usb_ep0_ClrRxPktRdy(0);
            } else {
                ep0_send_data(NULL, 0, 0);
            }
            break;
        }

#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
        /* CDC-ACM Class Requests */
        case CDC_REQ_SET_LINE_CODING: {
            if (req_len > 0 && (req_type_raw & 0x80) == 0) {
                ep0_ctx.is_cdc_line_coding = true;
                ep0_ctx.rx_remaining = req_len;
                usb_ep0_ClrRxPktRdy(0);
            } else {
                ep0_send_data(NULL, 0, 0);
            }
            break;
        }

        case CDC_REQ_GET_LINE_CODING: {
            ep0_send_data((const uint8_t *)&cdc_line_coding, sizeof(cdc_line_coding), req_len);
            break;
        }

        case CDC_REQ_SET_CONTROL_LINE_STATE: {
            cdc_dtr_active = (req_value & 0x01) != 0;
            cdc_rts_active = (req_value & 0x02) != 0;
            if (!cdc_dtr_active) {
                ep1_in_busy = false;
                cdc_zlp_pending = false;
            }
            ep0_send_data(NULL, 0, 0);
            if (cdc_dtr_active) {
                mcu_cdc_pump_tx();
            }
            break;
        }

        case CDC_REQ_SEND_BREAK: {
            ep0_send_data(NULL, 0, 0);
            break;
        }
#endif

        default:
            usb_ep0_Set_Stall(0);
            break;
        }
    } else {
        ep0_send_data(NULL, 0, 0);
    }
}

/* -------------------------------------------------------------------------
 * USB Interrupt Service Routine (IRQ 9)
 * ------------------------------------------------------------------------- */
__attribute__((interrupt("")))
static void mcu_usb_isr(void)
{
    u32 intr_usb = 0, intr_usbe = 0;
    u32 intr_tx = 0, intr_txe = 0;
    u32 intr_rx = 0, intr_rxe = 0;

    __asm__ volatile("ssync");
    usb_read_intr(0, &intr_usb, &intr_tx, &intr_rx);
    usb_read_intre(0, &intr_usbe, &intr_txe, &intr_rxe);

    intr_usb &= intr_usbe;
    intr_tx &= intr_txe;
    intr_rx &= intr_rxe;

    /* 1. USB Bus Reset Handler */
    if (intr_usb & INTRUSB_RESET_BABBLE) {
        dev_state = DEV_STATE_DEFAULT;
        dev_address = 0;
        active_config = 0;
        audio_alt_setting = 0;
        audio_buffering = true;
        audio_ring_head = 0;
        audio_ring_tail = 0;
        __asm__ volatile("csync");
        ep4_in_busy = false;
#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
        cdc_dtr_active = false;
        cdc_rts_active = false;
        ep1_in_busy = false;
        cdc_zlp_pending = false;
#endif
        ep0_ctx.tx_remaining = 0;
        ep0_ctx.rx_remaining = 0;
        ep0_ctx.set_address_pending = false;
#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
        ep0_ctx.is_cdc_line_coding = false;
#endif
        usb_write_faddr(0, 0);

        /* Clear all endpoint interrupt masks except EP0, EP1 OUT, and EP4 OUT */
        usb_clr_intr_txe(0, (u32)-1);
        usb_clr_intr_rxe(0, (u32)-1);
        usb_set_intr_txe(0, 0); /* EP0 */
#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
        usb_set_intr_rxe(0, 1); /* EP1 OUT (CDC Data) */
#endif
        usb_set_intr_rxe(0, 4); /* EP4 OUT (MIDI) */

        /* Re-arm endpoint configurations (ie=0 on TX to prevent empty FIFO interrupt storms) */
        usb_g_ep_config(0, 0,              USB_EP_TYPE_CTRL, 1, ep0_buffer, 64);
#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
        usb_g_ep_config(0, 1 | USB_DIR_IN, USB_EP_TYPE_BULK, 0, ep1_in_dma,  USB_CDC_DATA_EP_SIZE);
        usb_g_ep_config(0, 1,              USB_EP_TYPE_BULK, 1, ep1_out_dma, USB_CDC_DATA_EP_SIZE);
        usb_g_ep_config(0, 2 | USB_DIR_IN, USB_EP_TYPE_INTR, 0, ep2_in_dma,  USB_CDC_NOTIFY_EP_SIZE);
#endif
        usb_g_ep_config(0, 3 | USB_DIR_IN, USB_EP_TYPE_ISO,  0, ep3_in_dma,  USB_AUDIO_EP_SIZE);
        usb_g_ep_config(0, 4 | USB_DIR_IN, USB_EP_TYPE_BULK, 0, ep4_in_dma,  USB_MIDI_EP_SIZE);
        usb_g_ep_config(0, 4,              USB_EP_TYPE_BULK, 1, ep4_out_dma, USB_MIDI_EP_SIZE);

#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
        /* Hardware enable EP1 (CDC), EP2 (CDC Notify), EP4 (MIDI), disable EP3 until alt 1 */
        usb_enable_ep(0, 1);
        usb_enable_ep(0, 2);
#endif
        usb_enable_ep(0, 4);
        usb_disable_ep(0, 3);
        return;
    }

    /* 2. EP0 Control Endpoint */
    if (intr_tx & (1 << 0)) {
        u32 csr0 = usb_read_csr0(0);
        if (csr0 & CSR0P_SentStall) {
            usb_write_csr0(0, 0);
        } else if (csr0 & CSR0P_SetupEnd) {
            usb_write_csr0(0, CSR0P_ClrSetupEnd);
            ep0_ctx.tx_remaining = 0;
            ep0_ctx.rx_remaining = 0;
            ep0_ctx.set_address_pending = false;
#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
            ep0_ctx.is_cdc_line_coding = false;
#endif
        } else if (csr0 & CSR0P_RxPktRdy) {
            if (ep0_ctx.rx_remaining > 0) {
#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
                if (ep0_ctx.is_cdc_line_coding) {
                    __asm__ volatile("ssync; csync" ::: "memory");
                    uint16_t cdc_len = (ep0_ctx.rx_remaining < sizeof(cdc_line_coding)) ?
                                       ep0_ctx.rx_remaining : sizeof(cdc_line_coding);
                    memcpy(&cdc_line_coding, ep0_buffer, cdc_len);
                    ep0_ctx.is_cdc_line_coding = false;
                }
#endif
                ep0_ctx.rx_remaining = 0;
                usb_ep0_RxPktEnd(0);
            } else {
                __asm__ volatile("ssync; csync" ::: "memory");
                uint8_t setup_pkt[8];
                memcpy(setup_pkt, ep0_buffer, 8);
                ep0_handle_setup(setup_pkt);
            }
        } else if (ep0_ctx.tx_remaining > 0) {
            uint16_t chunk = ep0_ctx.tx_remaining;
            if (chunk > 64) chunk = 64;
            memcpy(ep0_buffer, ep0_ctx.tx_ptr, chunk);
            __asm__ volatile("ssync; csync" ::: "memory");
            JL_USB->EP0_CNT = chunk;
            __asm__ volatile("ssync; csync" ::: "memory");
            ep0_ctx.tx_ptr += chunk;
            ep0_ctx.tx_remaining -= chunk;
            if (ep0_ctx.tx_remaining == 0) {
                usb_ep0_TxPktEnd(0);
            } else {
                usb_write_csr0(0, CSR0P_TxPktRdy);
            }
        } else if (ep0_ctx.set_address_pending) {
            dev_address = ep0_ctx.pending_address;
            usb_write_faddr(0, dev_address);
            ep0_ctx.set_address_pending = false;
            dev_state = DEV_STATE_ADDRESSED;
        }
    }

    /* 3. EP3 IN (Isochronous Audio Streaming) */
    if (intr_tx & (1 << 3)) {
        if (audio_alt_setting == 1) {
            uint32_t head = audio_ring_head;
            uint32_t tail = audio_ring_tail;
            uint32_t available = (head >= tail) ?
                                 (head - tail) :
                                 (AUDIO_RING_FRAMES + head - tail);

            if (audio_buffering) {
                if (available >= AUDIO_TARGET_BUFFER) {
                    audio_buffering = false;
                }
            }

            uint16_t send_frames = 48;

            if (!audio_buffering && available >= 48) {
                /* Dynamic rate matching for asynchronous isochronous IN:
                 * Core 1 ALNK audio clock runs at ~48,077 Hz (+77 frames/sec vs 48,000 Hz host).
                 * When ring fill exceeds target (+32 frames), send 49 frames.
                 * If ring fill falls below target (-32 frames), send 47 frames.
                 * Host ALSA smoothly tracks variable packet lengths (UAC 1.0 standard). */
                if (available > (AUDIO_TARGET_BUFFER + 32)) {
                    send_frames = 49;
                } else if (available < (AUDIO_TARGET_BUFFER - 32) && available >= 47) {
                    send_frames = 47;
                }

                int16_t *dst = (int16_t *)ep3_in_dma;
                uint32_t first_part = AUDIO_RING_FRAMES - tail;
                if (first_part > send_frames) {
                    first_part = send_frames;
                }
                memcpy(dst, &audio_ring[tail * 2], first_part * 4);

                uint32_t second_part = send_frames - first_part;
                if (second_part > 0) {
                    memcpy(&dst[first_part * 2], &audio_ring[0], second_part * 4);
                }

                __asm__ volatile("csync");
                audio_ring_tail = (tail + send_frames) & AUDIO_RING_MASK;
                __asm__ volatile("csync");
            } else {
                /* Pre-buffering or underflow: send silence */
                send_frames = 48;
                memset(ep3_in_dma, 0, send_frames * 4);
                if (available < 48) {
                    audio_buffering = true;
                }
            }

            uint16_t bytes = send_frames * 4;
            __asm__ volatile("ssync; csync" ::: "memory");
            usb_g_ep_write(0, 3, ep3_in_dma, bytes);
        } else {
            usb_clr_intr_txe(0, 3);
        }
    }

    /* 4. EP4 OUT (Incoming MIDI from Host) */
    if (intr_rx & (1 << 4)) {
        uint8_t rx_buf[64];
        u32 rx_len = usb_g_ep_read(0, 4, rx_buf, sizeof(rx_buf), 0);
        if (rx_len > 0) {
            mcu_midi_process_rx_packet(rx_buf, rx_len);
        }
    }

    /* 5. EP4 IN (Outgoing MIDI to Host) */
    if (intr_tx & (1 << 4)) {
        usb_clr_intr_txe(0, 4);
        ep4_in_busy = false;
        mcu_midi_on_tx_complete();
    }

#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
    /* 6. EP1 IN (Outgoing CDC Serial to Host) */
    if (intr_tx & (1 << 1)) {
        usb_clr_intr_txe(0, 1);
        ep1_in_busy = false;
        mcu_cdc_pump_tx();
    }

    /* 7. EP1 OUT (Incoming CDC Serial from Host) */
    if (intr_rx & (1 << 1)) {
        uint8_t rx_buf[64];
        u32 rx_len = usb_g_ep_read(0, 1, rx_buf, sizeof(rx_buf), 0);
        if (rx_len > 0) {
            for (u32 i = 0; i < rx_len; i++) {
                uint16_t next_head = (cdc_rx_head + 1) & CDC_RX_RING_MASK;
                if (next_head != cdc_rx_tail) {
                    cdc_rx_ring[cdc_rx_head] = rx_buf[i];
                    __asm__ volatile("ssync; csync" ::: "memory");
                    cdc_rx_head = next_head;
                }
                if (cdc_rx_callback) {
                    cdc_rx_callback(rx_buf[i]);
                }
            }
        }
    }
#endif

    __asm__ volatile("ssync; csync" ::: "memory");
}

/* -------------------------------------------------------------------------
 * Public Driver APIs
 * ------------------------------------------------------------------------- */
int mcu_usb_init(void)
{
    /* 1. Configure PHY pins (D+/D-) */
    mcu_usb_phy_config();

    /* 2. Initialize USB SIE hardware & variable storage */
    usb_var_init(0, &usb_ep_addr);
    usb_g_sie_init(0);
    usb_write_power(0, 0x40); /* Full speed mode */
    usb_slave_init(0);

    /* 3. Pre-bind all endpoint DMA receive addresses */
    usb_set_dma_raddr(0, 0, ep0_buffer);
#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
    usb_set_dma_raddr(0, 1, ep1_out_dma);
#endif
    usb_set_dma_raddr(0, 2, ep0_buffer);
    usb_set_dma_raddr(0, 3, ep0_buffer);
    usb_set_dma_raddr(0, 4, ep4_out_dma);

    /* 4. Configure Endpoints (ie=0 on TX to avoid empty FIFO interrupt storms) */
    usb_g_ep_config(0, 0,              USB_EP_TYPE_CTRL, 1, ep0_buffer, 64);
#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
    usb_g_ep_config(0, 1 | USB_DIR_IN, USB_EP_TYPE_BULK, 0, ep1_in_dma,  USB_CDC_DATA_EP_SIZE);
    usb_g_ep_config(0, 1,              USB_EP_TYPE_BULK, 1, ep1_out_dma, USB_CDC_DATA_EP_SIZE);
    usb_g_ep_config(0, 2 | USB_DIR_IN, USB_EP_TYPE_INTR, 0, ep2_in_dma,  USB_CDC_NOTIFY_EP_SIZE);
#endif
    usb_g_ep_config(0, 3 | USB_DIR_IN, USB_EP_TYPE_ISO,  0, ep3_in_dma,  USB_AUDIO_EP_SIZE);
    usb_g_ep_config(0, 4 | USB_DIR_IN, USB_EP_TYPE_BULK, 0, ep4_in_dma,  USB_MIDI_EP_SIZE);
    usb_g_ep_config(0, 4,              USB_EP_TYPE_BULK, 1, ep4_out_dma, USB_MIDI_EP_SIZE);

#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
    usb_enable_ep(0, 1); /* Enable EP1 (CDC Data) */
    usb_enable_ep(0, 2); /* Enable EP2 (CDC Comm) */
#endif
    usb_enable_ep(0, 4); /* Enable EP4 (MIDI) */

    /* 5. Configure Interrupt Masks: Only Reset, EP0, EP1 OUT, and EP4 OUT initially */
    usb_write_intr_usbe(0, INTRUSB_RESET_BABBLE);
    usb_clr_intr_txe(0, (u32)-1);
    usb_clr_intr_rxe(0, (u32)-1);
    usb_set_intr_txe(0, 0); /* EP0 */
#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
    usb_set_intr_rxe(0, 1); /* EP1 OUT (CDC Data) */
#endif
    usb_set_intr_rxe(0, 4); /* EP4 OUT (MIDI) */

    /* 6. Register Hardware Interrupt (IRQ 9, priority 3 on Core 0) */
    request_irq(IRQ_USB_CTRL_IDX, 3, mcu_usb_isr, 0);

    /* 7. Enable global SIE interrupt */
    JL_USB->CON0 |= (1 << SIEIE);
    __asm__ volatile("csync");

    /* 8. Initialize MIDI layer */
    mcu_midi_init();

    return 0;
}

bool mcu_usb_is_configured(void)
{
    return (dev_state == DEV_STATE_CONFIGURED);
}

bool mcu_usb_audio_is_active(void)
{
    return (dev_state == DEV_STATE_CONFIGURED && audio_alt_setting == 1);
}

bool mcu_usb_ep4_tx(const uint8_t *data, size_t len)
{
    if (ep4_in_busy || len > sizeof(ep4_in_dma) || !data) {
        return false;
    }
    if (dev_state != DEV_STATE_CONFIGURED) {
        return false;
    }

    ep4_in_busy = true;
    memcpy(ep4_in_dma, data, len);
    __asm__ volatile("ssync; csync" ::: "memory");

    usb_set_intr_txe(0, 4);
    u32 ret = usb_g_ep_write(0, 4, ep4_in_dma, len);
    if (ret != len) {
        usb_clr_intr_txe(0, 4);
        ep4_in_busy = false;
        return false;
    }
    return true;
}

void mcu_usb_audio_push_samples(const int16_t *pcm16, size_t frames)
{
    if (!pcm16 || frames == 0 || frames >= AUDIO_RING_FRAMES || !mcu_usb_audio_is_active()) return;

    uint32_t head = audio_ring_head;
    uint32_t tail = audio_ring_tail;

    /* Free frames available in ring */
    uint32_t free_frames = (tail > head) ? (tail - head - 1) : (AUDIO_RING_FRAMES - 1 - (head - tail));
    if (free_frames < frames) {
        /* Ring buffer full: drop incoming block to protect I2S DAC deadline and consumer tail */
        return;
    }

    /* Copy samples into ring buffer using fast contiguous blocks */
    uint32_t first_part = AUDIO_RING_FRAMES - head;
    if (first_part > frames) {
        first_part = frames;
    }
    memcpy(&audio_ring[head * 2], pcm16, first_part * 4);

    uint32_t second_part = frames - first_part;
    if (second_part > 0) {
        memcpy(&audio_ring[0], &pcm16[first_part * 2], second_part * 4);
    }
    __asm__ volatile("csync");

    /* Single atomic update to head */
    audio_ring_head = (head + frames) & AUDIO_RING_MASK;
    __asm__ volatile("csync");
}

void mcu_usb_poll(void)
{
    mcu_midi_poll_tx();
    mcu_cdc_pump_tx();
}

#if defined(ENABLE_SERIAL) && ENABLE_SERIAL
/* -------------------------------------------------------------------------
 * CDC Virtual COM Port & Serial Logging Pipeline
 * ------------------------------------------------------------------------- */
static inline void cdc_ring_push(uint8_t b)
{
    uint16_t head = cdc_tx_head;
    uint16_t next_head = (head + 1) & CDC_TX_RING_MASK;
    if (next_head == cdc_tx_tail) {
        cdc_dropped_bytes++;
        return;
    }
    cdc_tx_ring[head] = b;
    __asm__ volatile("csync");
    cdc_tx_head = next_head;
}

void mcu_cdc_putc(char c)
{
    if (c == '\n') {
        cdc_ring_push('\r');
    }
    cdc_ring_push((uint8_t)c);
    mcu_cdc_pump_tx();
}

void mcu_cdc_write(const void *buf, size_t len)
{
    if (!buf || len == 0) return;
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        if (p[i] == '\n') {
            cdc_ring_push('\r');
        }
        cdc_ring_push(p[i]);
    }
    mcu_cdc_pump_tx();
}

void mcu_cdc_puts(const char *str)
{
    if (str) {
        mcu_cdc_write(str, strlen(str));
    }
}

int mcu_cdc_getc(void)
{
    uint16_t head = cdc_rx_head;
    uint16_t tail = cdc_rx_tail;
    if (head == tail) {
        return -1;
    }
    uint8_t b = cdc_rx_ring[tail];
    __asm__ volatile("csync");
    cdc_rx_tail = (tail + 1) & CDC_RX_RING_MASK;
    return (int)b;
}

int mcu_cdc_read(void *buf, size_t max_len)
{
    if (!buf || max_len == 0) return 0;
    uint8_t *p = (uint8_t *)buf;
    size_t count = 0;
    while (count < max_len) {
        int c = mcu_cdc_getc();
        if (c < 0) break;
        p[count++] = (uint8_t)c;
    }
    return (int)count;
}

bool mcu_cdc_is_connected(void)
{
    return (cdc_dtr_active && dev_state == DEV_STATE_CONFIGURED);
}

uint32_t mcu_cdc_get_dropped_bytes(void)
{
    return cdc_dropped_bytes;
}

uint32_t mcu_cdc_get_tx_pending(void)
{
    uint16_t head = cdc_tx_head;
    uint16_t tail = cdc_tx_tail;
    return (head >= tail) ? (uint32_t)(head - tail)
                          : (uint32_t)(CDC_TX_RING_SIZE + head - tail);
}

void mcu_cdc_set_rx_callback(void (*cb)(uint8_t byte))
{
    cdc_rx_callback = cb;
}

void mcu_cdc_pump_tx(void)
{
    if (!cdc_dtr_active || dev_state != DEV_STATE_CONFIGURED) {
        return;
    }

    int flags = cdc_irq_save();

    if (ep1_in_busy) {
        cdc_irq_restore(flags);
        return;
    }

    uint16_t head = cdc_tx_head;
    uint16_t tail = cdc_tx_tail;
    uint16_t count = (head >= tail) ? (head - tail) : (CDC_TX_RING_SIZE + head - tail);

    if (count == 0) {
        if (cdc_zlp_pending) {
            cdc_zlp_pending = false;
            ep1_in_busy = true;
            usb_set_intr_txe(0, 1);
            u32 ret = usb_g_ep_write(0, 1, ep1_in_dma, 0);
            if (ret != 0) {
                usb_clr_intr_txe(0, 1);
                ep1_in_busy = false;
            }
        }
        cdc_irq_restore(flags);
        return;
    }

    uint16_t chunk = count;
    if (chunk > USB_CDC_DATA_EP_SIZE) {
        chunk = USB_CDC_DATA_EP_SIZE;
    }

    for (uint16_t i = 0; i < chunk; i++) {
        ep1_in_dma[i] = cdc_tx_ring[(tail + i) & CDC_TX_RING_MASK];
    }
    __asm__ volatile("csync");
    cdc_tx_tail = (tail + chunk) & CDC_TX_RING_MASK;

    /* A ZLP is required if this packet exactly filled the 64-byte max packet size,
     * and the ring has now become completely empty. */
    cdc_zlp_pending = (chunk == USB_CDC_DATA_EP_SIZE);
    ep1_in_busy = true;

    __asm__ volatile("ssync; csync" ::: "memory");
    usb_set_intr_txe(0, 1);
    u32 ret = usb_g_ep_write(0, 1, ep1_in_dma, chunk);
    if (ret != chunk) {
        usb_clr_intr_txe(0, 1);
        ep1_in_busy = false;
    }

    cdc_irq_restore(flags);
}
#endif

/* HAL status queries */
bool hal_usb_is_configured(void)
{
    return mcu_usb_is_configured();
}

bool hal_usb_audio_is_active(void)
{
    return mcu_usb_audio_is_active();
}

void hal_usb_audio_push_samples(const int16_t *pcm16, size_t frames)
{
    mcu_usb_audio_push_samples(pcm16, frames);
}

