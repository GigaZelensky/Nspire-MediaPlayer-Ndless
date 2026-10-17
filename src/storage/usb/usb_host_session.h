#ifndef NDVIDEO_USB_HOST_SESSION_H
#define NDVIDEO_USB_HOST_SESSION_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
/* Prepare/release at a native clock/mapping boundary. One app-owned lease
 * covers both bulk storage and isochronous audio. */
bool usb_host_session_prepare(uint32_t (*)(void), bool (*)(void *), void *, char *, size_t);
bool usb_host_session_active(void);
void usb_host_session_release(void);
void usb_host_session_debug(FILE *);
#endif
