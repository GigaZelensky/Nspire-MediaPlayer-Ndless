#ifndef NDVIDEO_USB_AUTOCONNECT_H
#define NDVIDEO_USB_AUTOCONNECT_H
#include <stdbool.h>
#include <stdint.h>
#include "usb_registers.h"

enum { USB_AUTO_ADAPTER = 1U, USB_AUTO_DEVICE = 2U };
#define USB_AUTO_UNKNOWN UINT32_MAX

static inline unsigned usb_auto_connect_presence(const UsbRegisters *r, uint32_t cap,
                                                uint32_t identity, uint32_t otg, uint32_t port)
{
    if (!usb_registers_id_readable(r, cap, identity, otg)) return USB_AUTO_UNKNOWN;
    if (otg & r->role_mask) return 0;
    /* A sleeping host's port register is not authoritative yet. The adapter
     * alone starts discovery; enumeration still validates the host normally. */
    bool host_valid = (cap & 0xffff00ffU) == (r->classic ? 0x01000040U : 0x01000010U);
    return USB_AUTO_ADAPTER |
        (host_valid && port != UINT32_MAX && (port & 1U) ? USB_AUTO_DEVICE : 0U);
}
typedef struct {
    unsigned stable, candidate;
    uint32_t changed_at;
} UsbAutoConnect;

/* Observe insertion, not every frame spent connected. Commit removal too,
 * so replacing a drive in an adapter can trigger another attempt. Brief
 * disconnect noise must not reopen a cancelled/failed connection window. */
static inline bool usb_auto_connect_poll(UsbAutoConnect *s, unsigned observed,
                                        uint32_t now, bool mounted)
{
    if (observed == USB_AUTO_UNKNOWN) {
        /* Require a fresh continuous debounce when the PHY becomes readable. */
        s->changed_at = now;
        return false;
    }
    if (observed != s->candidate) {
        s->candidate = observed;
        s->changed_at = now;
    }
    if ((uint32_t)(now - s->changed_at) < 250U) return false;
    unsigned inserted = observed & ~s->stable;
    s->stable = observed;
    return !mounted && (observed & USB_AUTO_ADAPTER) && inserted;
}

/* A manual attempt or a completed connection consumes the current state.
 * It must not cause another popup immediately after Cancel or an error. */
static inline void usb_auto_connect_acknowledge(UsbAutoConnect *s, unsigned observed,
                                               uint32_t now)
{
    if (observed == USB_AUTO_UNKNOWN) return;
    s->stable = s->candidate = observed;
    s->changed_at = now;
}
#endif
