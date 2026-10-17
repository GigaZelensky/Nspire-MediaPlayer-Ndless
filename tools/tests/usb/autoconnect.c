#include "usb_autoconnect.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    UsbRegisters r;
    assert(usb_registers_for_asic(0x202, &r));
    /* Recorded CX II host/device roles, plus a stopped capability block.
     * Watching insertion must not require an already enumerated host. */
    assert(usb_auto_connect_presence(&r, 0x01000010, 0, 0x008e0010, 0x88d) == 3);
    assert(usb_auto_connect_presence(&r, 0, 0, 0x000e0010, 0) == USB_AUTO_ADAPTER);
    assert(usb_auto_connect_presence(&r, 0, 0, 0x00310020, 0) == 0);
    assert(usb_auto_connect_presence(&r, 0, 0, 0, 0) == USB_AUTO_UNKNOWN);
    assert(usb_auto_connect_presence(&r, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX) == USB_AUTO_UNKNOWN);
    assert(usb_auto_connect_presence(&r, 0xdeadbeef, 0, 0x000e0010, 0) == USB_AUTO_UNKNOWN);
    assert(usb_registers_for_asic(0x101, &r));
    assert(usb_auto_connect_presence(&r, 0, 0xe241fa05, 0, 0) == USB_AUTO_ADAPTER);
    assert(usb_auto_connect_presence(&r, 0, 0xe241fa05, 1U << 8, 0) == 0);
    assert(usb_auto_connect_presence(&r, 0, 0, 0, 0) == USB_AUTO_UNKNOWN);
    UsbAutoConnect s = {0};
    assert(!usb_auto_connect_poll(&s, 0, 1000, false));
    /* An adapter alone must start discovery: no initialized host is needed. */
    assert(!usb_auto_connect_poll(&s, USB_AUTO_ADAPTER, 1100, false));
    assert(!usb_auto_connect_poll(&s, USB_AUTO_ADAPTER, 1349, false));
    assert(usb_auto_connect_poll(&s, USB_AUTO_ADAPTER, 1350, false));
    assert(!usb_auto_connect_poll(&s, USB_AUTO_ADAPTER, 5000, false));
    assert(!usb_auto_connect_poll(&s, 3, 5100, false));
    assert(usb_auto_connect_poll(&s, 3, 5350, false));
    /* Cancel/error consumes the state; a noisy disconnect cannot reopen it. */
    usb_auto_connect_acknowledge(&s, 3, 5400);
    assert(!usb_auto_connect_poll(&s, 0, 5500, false));
    assert(!usb_auto_connect_poll(&s, 3, 5600, false));
    assert(!usb_auto_connect_poll(&s, 3, 5900, false));
    /* Replace just the drive, leaving the adapter attached. */
    assert(!usb_auto_connect_poll(&s, 1, 6000, false));
    assert(!usb_auto_connect_poll(&s, 1, 6250, false));
    assert(!usb_auto_connect_poll(&s, 3, 6300, false));
    assert(usb_auto_connect_poll(&s, 3, 6550, false));
    s = (UsbAutoConnect){0};
    assert(!usb_auto_connect_poll(&s, 3, 10, true));
    assert(!usb_auto_connect_poll(&s, 3, 260, true));
    assert(!usb_auto_connect_poll(&s, 3, 1000, false));
    /* A fresh launch with a preconnected drive gets exactly one attempt. */
    s = (UsbAutoConnect){0};
    assert(!usb_auto_connect_poll(&s, 3, 10, false));
    assert(usb_auto_connect_poll(&s, 3, 260, false));
    assert(!usb_auto_connect_poll(&s, 3, 1000, false));
    usb_auto_connect_acknowledge(&s, 0, UINT32_MAX - 200U);
    assert(!usb_auto_connect_poll(&s, 1, UINT32_MAX - 100U, false));
    assert(!usb_auto_connect_poll(&s, 1, 148U, false));
    assert(usb_auto_connect_poll(&s, 1, 149U, false));
    /* An unreadable sample neither acknowledges a real insertion nor
     * invents a removal/reinsertion after Cancel. Debounce must be continuous. */
    s = (UsbAutoConnect){0};
    assert(!usb_auto_connect_poll(&s, 1, 100, false));
    assert(!usb_auto_connect_poll(&s, USB_AUTO_UNKNOWN, 349, false));
    assert(!usb_auto_connect_poll(&s, 1, 350, false));
    assert(!usb_auto_connect_poll(&s, 1, 598, false));
    assert(usb_auto_connect_poll(&s, 1, 599, false));
    usb_auto_connect_acknowledge(&s, 1, 600);
    usb_auto_connect_acknowledge(&s, USB_AUTO_UNKNOWN, 601);
    assert(s.stable == 1 && s.candidate == 1);
    assert(!usb_auto_connect_poll(&s, USB_AUTO_UNKNOWN, 700, false));
    assert(!usb_auto_connect_poll(&s, 1, 1000, false));
    assert(!usb_auto_connect_poll(&s, 0, 1100, false));
    assert(!usb_auto_connect_poll(&s, 0, 1350, false));
    assert(!usb_auto_connect_poll(&s, 1, 1400, false));
    assert(usb_auto_connect_poll(&s, 1, 1650, false));
    usb_auto_connect_acknowledge(&s,3,2000);
    usb_auto_connect_removed(&s,3,2100); /* cleanup missed the brief low level */
    assert(!usb_auto_connect_poll(&s,3,2110,false));
    assert(usb_auto_connect_poll(&s,3,2360,false));
    usb_auto_connect_acknowledge(&s,3,2400);
    assert(!usb_auto_connect_poll(&s,3,3000,false));
    usb_auto_connect_removed(&s,USB_AUTO_UNKNOWN,3100);
    assert(!usb_auto_connect_poll(&s,3,3110,false));
    assert(usb_auto_connect_poll(&s,3,3360,false));
    puts("PASS automatic USB insertion: adapter-first discovery, preconnected drive, debounce, cancel suppression, replacement, mounted state and timer wrap");
}
