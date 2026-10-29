#ifndef FENESTRA_USB_H
#define FENESTRA_USB_H
#include "fenestra_debug.h"

#define FX_USB_PACKET 64u
typedef enum { FX_USB_STALL, FX_USB_IN, FX_USB_OUT, FX_USB_ACK } FxUsbReplyKind;
typedef struct { FxUsbReplyKind kind; const uint8_t *data; uint32_t bytes; } FxUsbReply;
/* Full-speed CDC ACM function engine, not an MMIO/PHY driver. A verified
 * device controller must provide SETUP/data/status/reset and packet events.
 * Descriptors here describe a standalone laboratory device, not Nikon PTP.
 * Physical/composite interface allocation and VID/PID are platform-owned. */
typedef struct {
    FxDebug *debug;
    uint64_t boot_nonce, nonce, now;
    uint8_t device[18], configuration[75], response[128], line_coding[7], pending_line[7];
    uint8_t tail[FX_USB_PACKET];
    uint32_t address, configured, dtr, rts, halted, pending, pending_value;
    uint32_t tail_bytes, notification_pending;
} FxUsb;

FxResult fx_usb_init(FxUsb *, FxDebug *, uint16_t vid, uint16_t pid, uint64_t boot_nonce);
void fx_usb_reset(FxUsb *);
FxUsbReply fx_usb_setup(FxUsb *, const uint8_t setup[8]);
FxUsbReply fx_usb_control_out(FxUsb *, const uint8_t *, uint32_t bytes);
FxResult fx_usb_status_complete(FxUsb *, uint64_t now_ms);
FxResult fx_usb_bulk_out(FxUsb *, const uint8_t *, uint32_t bytes, uint64_t now_ms);
FxResult fx_usb_bulk_in(FxUsb *, uint8_t *, uint32_t capacity, uint32_t *bytes, uint64_t now_ms);
FxResult fx_usb_notification(FxUsb *, uint8_t out[10], uint32_t *bytes);
#endif
