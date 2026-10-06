#ifndef NDVIDEO_USB_SCSI_H
#define NDVIDEO_USB_SCSI_H
#include "usb_transport.h"
typedef enum {
    USB_SCSI_IDLE,
    USB_SCSI_CBW,
    USB_SCSI_DATA,
    USB_SCSI_CSW,
    USB_SCSI_DONE,
    USB_SCSI_ERROR
} UsbScsiPhase;
typedef struct {
    UsbTransport *transport;
    void *data;
    uint32_t tag, expected, transferred;
    UsbScsiPhase phase;
    unsigned error;
    uint8_t command_status;
    bool input;
} UsbScsi;
enum { USB_SCSI_PROTOCOL = 100, USB_SCSI_COMMAND_FAILED, USB_SCSI_SHORT_DATA };
void usb_scsi_init(UsbScsi *, UsbTransport *);
bool usb_scsi_begin(UsbScsi *, const uint8_t *, unsigned, void *, unsigned, bool);
int usb_scsi_step(UsbScsi *);
bool usb_scsi_busy(const UsbScsi *);
#endif
