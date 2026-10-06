#include "usb_transport.h"
#include <string.h>

enum {
    HC_COMMAND = 0x10,
    HC_STATUS = 0x14,
    HC_INTERRUPTS = 0x18,
    HC_ASYNC_LIST = 0x28,
    HC_PORT = 0x30
};
enum {
    COMMAND_RUN = 1,
    COMMAND_RESET = 2,
    COMMAND_ASYNC = 32,
    COMMAND_DOORBELL = 64,
    STATUS_INTERRUPT = 1,
    STATUS_ERROR = 2,
    STATUS_FATAL = 16,
    STATUS_HALTED = 4096,
    STATUS_ASYNC = 32768
};
#define TD_ACTIVE 0x80U
#define TD_ERROR 0x7cU
#define TD_TOGGLE 0x80000000U
static uint32_t read_reg(UsbTransport *t, unsigned offset)
{
    return t->bus.read(t->bus.context, offset);
}
static void write_reg(UsbTransport *t, unsigned offset, uint32_t value)
{
    t->bus.write(t->bus.context, offset, value);
}
static void barrier(UsbTransport *t)
{
    t->bus.barrier(t->bus.context);
}
static uint32_t now(UsbTransport *t)
{
    return t->bus.ticks(t->bus.context);
}
static void phase(UsbTransport *t, UsbTransferPhase value)
{
    t->phase = value;
    t->phase_started = now(t);
    t->phase_polls = 0;
}
static bool expired(UsbTransport *t, unsigned milliseconds)
{
    /* A stopped clock cannot create an infinite retry loop. */
    uint64_t timeout = ((uint64_t)t->tick_hz * milliseconds + 999U) / 1000U;
    return ++t->phase_polls >= 65536U || (uint32_t)(now(t) - t->phase_started) >= timeout;
}
void usb_transport_init(UsbTransport *t, UsbBus bus, uint8_t *bounce, UsbQueueHead *queue,
                        UsbQtd *descriptor, uint32_t bounce_pa, uint32_t queue_pa,
                        uint32_t descriptor_pa, uint32_t tick_hz)
{
    memset(t, 0, sizeof(*t));
    t->bus = bus;
    t->bounce = bounce;
    t->queue = queue;
    t->descriptor = descriptor;
    t->bounce_physical = bounce_pa;
    t->queue_physical = queue_pa;
    t->descriptor_physical = descriptor_pa;
    t->tick_hz = tick_hz;
}
bool usb_transport_quiescent(const UsbTransport *t)
{
    return !t || (!t->owns_schedule && !t->poisoned);
}
static int finish(UsbTransport *t)
{
    if (t->error) {
        ++t->errors;
        t->phase = USB_TRANSFER_ERROR;
        return -1;
    }
    ++t->transfers;
    t->phase = USB_TRANSFER_DONE;
    return 1;
}
static void restore(UsbTransport *t)
{
    write_reg(t, HC_ASYNC_LIST, t->original_list);
    write_reg(t, HC_STATUS, (STATUS_INTERRUPT | STATUS_ERROR) & ~t->original_pending);
    write_reg(t, HC_COMMAND, t->original_command);
    write_reg(t, HC_INTERRUPTS, t->original_interrupts);
    barrier(t);
    t->owns_schedule = false;
}
bool usb_transport_begin(UsbTransport *t, bool input, const void *data, unsigned bytes)
{
    if (!t || t->owns_schedule || t->poisoned || !bytes || bytes > 4096 || (!input && !data) ||
        !t->address || t->address > 127 || !(t->endpoint_in & 15) || !(t->endpoint_out & 15) ||
        (t->bounce_physical & 4095) || (t->queue_physical & 31) || (t->descriptor_physical & 31) ||
        !t->tick_hz)
        return false;
    t->error = USB_TRANSPORT_OK;
    t->cancelled = false;
    t->bytes = bytes;
    t->actual = 0;
    t->input = input;
    t->started = now(t);
    t->total_polls = 0;
    t->original_command = read_reg(t, HC_COMMAND);
    t->original_pending = read_reg(t, HC_STATUS);
    if (!(read_reg(t, HC_PORT) & 1)) {
        t->error = USB_TRANSPORT_DISCONNECTED;
        finish(t);
        return false;
    }
    if (!(read_reg(t, HC_PORT) & 4) || !(t->original_command & COMMAND_RUN) ||
        (t->original_command & (COMMAND_RESET | COMMAND_DOORBELL)) ||
        (t->original_pending & (STATUS_HALTED | STATUS_FATAL))) {
        t->error = USB_TRANSPORT_NOT_READY;
        finish(t);
        return false;
    }
    t->original_interrupts = read_reg(t, HC_INTERRUPTS);
    t->original_list = read_reg(t, HC_ASYNC_LIST);
    memset(t->queue, 0, sizeof(*t->queue));
    memset(t->descriptor, 0, sizeof(*t->descriptor));
    if (!input)
        memcpy(t->bounce, data, bytes);
    t->descriptor->next = t->descriptor->alternate = 1;
    t->descriptor->token = (bytes << 16) |
                           ((input ? t->toggle_in : t->toggle_out) ? TD_TOGGLE : 0) | 0x8000U |
                           (3U << 10) | (input ? 0x100U : 0) | TD_ACTIVE;
    t->descriptor->buffer[0] = t->bounce_physical;
    t->queue->link = t->queue_physical | 2;
    t->queue->endpoint = t->address | (((input ? t->endpoint_in : t->endpoint_out) & 15U) << 8) |
                         (2U << 12) | (1U << 14) | (1U << 15) | (512U << 16) | (4U << 28);
    t->queue->capabilities = 1U << 30;
    t->queue->next = t->descriptor_physical;
    t->queue->alternate = 1;
    t->bus.clean(t->bus.context, t->bounce, 4096);
    t->bus.clean(t->bus.context, t->queue, 64);
    t->bus.clean(t->bus.context, t->descriptor, 32);
    barrier(t);
    write_reg(t, HC_INTERRUPTS, 0);
    write_reg(t, HC_COMMAND, t->original_command & ~COMMAND_ASYNC);
    barrier(t);
    t->owns_schedule = true;
    phase(t, USB_TRANSFER_STOP_NATIVE);
    return true;
}
int usb_transport_step(UsbTransport *t)
{
    if (!t)
        return -1;
    if (t->phase == USB_TRANSFER_DONE)
        return 1;
    if (t->phase == USB_TRANSFER_ERROR)
        return -1;
    if (t->phase == USB_TRANSFER_IDLE)
        return 0;
    ++t->total_polls;
    uint32_t status = read_reg(t, HC_STATUS);
    switch (t->phase) {
    case USB_TRANSFER_STOP_NATIVE:
        if (!(status & STATUS_ASYNC)) {
            if (t->cancelled) {
                restore(t);
                t->error = USB_TRANSPORT_ARGUMENT;
                return finish(t);
            }
            write_reg(t, HC_ASYNC_LIST, t->queue_physical);
            barrier(t);
            write_reg(t, HC_COMMAND, t->original_command | COMMAND_ASYNC);
            barrier(t);
            phase(t, USB_TRANSFER_START);
        } else if (expired(t, 20)) {
            t->error = USB_TRANSPORT_TIMEOUT;
            ++t->timeouts;
            /* Our queue was never installed; do not touch a live native
             * schedule's list pointer or acknowledge its interrupts. */
            write_reg(t, HC_COMMAND, t->original_command);
            write_reg(t, HC_INTERRUPTS, t->original_interrupts);
            barrier(t);
            t->owns_schedule = false;
            return finish(t);
        }
        break;
    case USB_TRANSFER_START:
        if (status & STATUS_ASYNC)
            phase(t, USB_TRANSFER_WAIT);
        else if (expired(t, 20)) {
            t->error = USB_TRANSPORT_TIMEOUT;
            ++t->timeouts;
            write_reg(t, HC_COMMAND, read_reg(t, HC_COMMAND) & ~COMMAND_ASYNC);
            barrier(t);
            phase(t, USB_TRANSFER_STOP_PRIVATE);
        }
        break;
    case USB_TRANSFER_WAIT:
        t->bus.invalidate(t->bus.context, t->descriptor, 32);
        t->last_token = ((volatile UsbQtd *)t->descriptor)->token;
        if (!(read_reg(t, HC_PORT) & 1))
            t->error = USB_TRANSPORT_DISCONNECTED;
        else if (status & (STATUS_HALTED | STATUS_FATAL))
            t->error = USB_TRANSPORT_HARDWARE;
        else if ((t->last_token & TD_ACTIVE) && expired(t, 1000)) {
            t->error = USB_TRANSPORT_TIMEOUT;
            ++t->timeouts;
        }
        if (!(t->last_token & TD_ACTIVE) || t->error || t->cancelled) {
            write_reg(t, HC_COMMAND, read_reg(t, HC_COMMAND) & ~COMMAND_ASYNC);
            barrier(t);
            phase(t, USB_TRANSFER_STOP_PRIVATE);
        }
        break;
    case USB_TRANSFER_STOP_PRIVATE:
        if (!(status & STATUS_ASYNC)) {
            t->bus.invalidate(t->bus.context, t->descriptor, 32);
            t->last_token = ((volatile UsbQtd *)t->descriptor)->token;
            unsigned remaining = (t->last_token >> 16) & 0x7fffU;
            if (!t->error && ((t->last_token & (TD_ACTIVE | TD_ERROR)) || remaining > t->bytes ||
                              (status & (STATUS_HALTED | STATUS_FATAL))))
                t->error = USB_TRANSPORT_HARDWARE;
            if (!t->error && !(read_reg(t, HC_PORT) & 1))
                t->error = USB_TRANSPORT_DISCONNECTED;
            if (!t->error) {
                t->actual = t->bytes - remaining;
                if (t->input) {
                    t->toggle_in = !!(t->last_token & TD_TOGGLE);
                    t->bus.invalidate(t->bus.context, t->bounce, 4096);
                } else
                    t->toggle_out = !!(t->last_token & TD_TOGGLE);
            }
            restore(t);
            return finish(t);
        } else if (expired(t, 20)) {
            write_reg(t, HC_COMMAND, read_reg(t, HC_COMMAND) & ~COMMAND_RUN);
            barrier(t);
            t->poisoned = true;
            t->error = USB_TRANSPORT_UNSAFE_STOP;
            phase(t, USB_TRANSFER_FORCE_STOP);
        }
        break;
    case USB_TRANSFER_FORCE_STOP:
        if (status & STATUS_HALTED) {
            write_reg(t, HC_ASYNC_LIST, t->original_list);
            barrier(t);
            t->owns_schedule = false;
            return finish(t);
        }
        if (expired(t, 20))
            return finish(t); /* Arena must remain allocated. */
        break;
    default:
        return -1;
    }
    return 0;
}
void usb_transport_cancel(UsbTransport *t)
{
    if (t && t->owns_schedule)
        t->cancelled = true;
}
