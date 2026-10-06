#ifndef NDVIDEO_NATIVE_USB_UI_H
#define NDVIDEO_NATIVE_USB_UI_H
#include <stdbool.h>
#include <stdio.h>
/* Call with genuine native SRAM and IRQ/FIQ masked. The reservation remains
 * across connection windows so the OS cannot redraw over the player. */
int native_usb_ui_acquire(bool (*)(void *), void *);
int native_usb_ui_reassert(void);
int native_usb_ui_release(void);
bool native_usb_ui_active(void);
const char *native_usb_ui_stage(void);
void native_usb_ui_debug(FILE *);
#endif
