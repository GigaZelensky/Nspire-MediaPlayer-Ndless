#ifndef NDVIDEO_USB_ROUTE_H
#define NDVIDEO_USB_ROUTE_H
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    uint8_t address, speed, parent, port, tt_hub, tt_port;
    bool tt_multi;
} UsbDeviceRoute;
/* Read-only native USBdi state, while the same bus/tree lease is held.
 * An unknown alternate must never be treated as alternate zero. */
typedef struct {
    uint8_t configuration, alternate;
    bool alternate_known;
} UsbDeviceState;
#endif
