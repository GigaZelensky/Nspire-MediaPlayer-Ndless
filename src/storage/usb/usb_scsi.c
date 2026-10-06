#include "usb_scsi.h"
#include <string.h>
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}
void usb_scsi_init(UsbScsi *s, UsbTransport *t)
{
    memset(s, 0, sizeof(*s));
    s->transport = t;
}
bool usb_scsi_busy(const UsbScsi *s)
{
    return s && s->phase >= USB_SCSI_CBW && s->phase <= USB_SCSI_CSW;
}
static int failed(UsbScsi *s, unsigned error)
{
    s->error = error;
    s->phase = USB_SCSI_ERROR;
    return -1;
}
bool usb_scsi_begin(UsbScsi *s, const uint8_t *command, unsigned command_bytes, void *data,
                    unsigned bytes, bool input)
{
    if (!s || usb_scsi_busy(s) || !command || !command_bytes || command_bytes > 16 ||
        bytes > 4096 || (bytes && !data) || !usb_transport_quiescent(s->transport))
        return false;
    uint8_t cbw[31] = {0};
    put32(cbw, 0x43425355U);
    put32(cbw + 4, ++s->tag);
    put32(cbw + 8, bytes);
    cbw[12] = input ? 0x80 : 0;
    cbw[14] = (uint8_t)command_bytes;
    memcpy(cbw + 15, command, command_bytes);
    s->data = data;
    s->expected = bytes;
    s->transferred = 0;
    s->error = 0;
    s->command_status = 0;
    s->input = input;
    if (!usb_transport_begin(s->transport, false, cbw, sizeof(cbw))) {
        failed(s, s->transport->error ? s->transport->error : USB_TRANSPORT_ARGUMENT);
        return false;
    }
    s->phase = USB_SCSI_CBW;
    return true;
}
int usb_scsi_step(UsbScsi *s)
{
    if (!s)
        return -1;
    if (s->phase == USB_SCSI_DONE)
        return 1;
    if (s->phase == USB_SCSI_ERROR)
        return -1;
    if (s->phase == USB_SCSI_IDLE)
        return 0;
    int result = usb_transport_step(s->transport);
    if (result < 0)
        return failed(s, s->transport->error);
    if (!result)
        return 0;
    UsbTransport *t = s->transport;
    if (s->phase == USB_SCSI_CBW) {
        if (t->actual != 31)
            return failed(s, USB_SCSI_PROTOCOL);
        if (s->expected) {
            if (!usb_transport_begin(t, s->input, s->data, s->expected))
                return failed(s, t->error ? t->error : USB_SCSI_PROTOCOL);
            s->phase = USB_SCSI_DATA;
            return 0;
        }
    } else if (s->phase == USB_SCSI_DATA) {
        s->transferred = t->actual;
        if (s->input && s->transferred)
            memcpy(s->data, t->bounce, s->transferred);
    } else if (s->phase == USB_SCSI_CSW) {
        if (t->actual != 13 || le32(t->bounce) != 0x53425355U || le32(t->bounce + 4) != s->tag ||
            le32(t->bounce + 8) > s->expected || t->bounce[12] > 1)
            return failed(s, USB_SCSI_PROTOCOL);
        s->command_status = t->bounce[12];
        if (s->command_status)
            return failed(s, USB_SCSI_COMMAND_FAILED);
        if (le32(t->bounce + 8) || s->transferred != s->expected)
            return failed(s, USB_SCSI_SHORT_DATA);
        s->phase = USB_SCSI_DONE;
        return 1;
    } else
        return failed(s, USB_SCSI_PROTOCOL);
    if (!usb_transport_begin(t, true, NULL, 13))
        return failed(s, t->error ? t->error : USB_SCSI_PROTOCOL);
    s->phase = USB_SCSI_CSW;
    return 0;
}
