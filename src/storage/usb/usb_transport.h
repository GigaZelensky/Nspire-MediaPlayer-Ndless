#ifndef NDVIDEO_USB_TRANSPORT_H
#define NDVIDEO_USB_TRANSPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define USB_READ_BYTES 16384U

/* The hardware queue and bounce buffer are owned until the async schedule
 * has stopped. A pending step never spins waiting for the controller. */
typedef struct {
    void *context;
    uint32_t (*read)(void *, unsigned);
    void (*write)(void *, unsigned, uint32_t);
    uint32_t (*ticks)(void *);
    void (*clean)(void *, void *, size_t);
    void (*invalidate)(void *, void *, size_t);
    void (*barrier)(void *);
} UsbBus;
typedef struct {
    uint32_t next, alternate, token, buffer[5];
} UsbQtd;
typedef struct {
    uint32_t link, endpoint, capabilities, current, next, alternate, token, buffer[5];
    uint32_t reserved[4];
} UsbQueueHead;
typedef enum {
    USB_TRANSFER_IDLE,
    USB_TRANSFER_STOP_NATIVE,
    USB_TRANSFER_START,
    USB_TRANSFER_WAIT,
    USB_TRANSFER_STOP_PRIVATE,
    USB_TRANSFER_FORCE_STOP,
    USB_TRANSFER_DONE,
    USB_TRANSFER_ERROR
} UsbTransferPhase;
typedef enum {
    USB_TRANSPORT_OK,
    USB_TRANSPORT_ARGUMENT,
    USB_TRANSPORT_DISCONNECTED,
    USB_TRANSPORT_NOT_READY,
    USB_TRANSPORT_TIMEOUT,
    USB_TRANSPORT_HARDWARE,
    USB_TRANSPORT_SHORT,
    USB_TRANSPORT_UNSAFE_STOP
} UsbTransportError;
typedef struct {
    UsbBus bus;
    uint8_t *bounce;
    UsbQueueHead *queue;
    UsbQtd *descriptor;
    uint32_t bounce_physical, queue_physical, descriptor_physical;
    uint32_t original_command, original_interrupts, original_list, original_pending;
    uint32_t started, phase_started, phase_polls, total_polls, phase_last_tick;
    uint32_t timeout_elapsed, timeout_polls;
    UsbTransferPhase timeout_phase;
    uint32_t last_token, transfers, timeouts, errors;
    uint32_t tick_hz;
    unsigned bytes, actual;
    unsigned max_bytes;
    uint8_t address, endpoint_in, endpoint_out, toggle_in, toggle_out;
    uint16_t packet_in, packet_out;
    bool high_speed;
    bool input, owns_schedule, poisoned, cancelled;
    UsbTransferPhase phase;
    UsbTransportError error;
} UsbTransport;
void usb_transport_init(UsbTransport *, UsbBus, uint8_t *, UsbQueueHead *, UsbQtd *, uint32_t,
                        uint32_t, uint32_t, uint32_t);
bool usb_transport_begin(UsbTransport *, bool, const void *, unsigned);
/* 0 pending, 1 complete, -1 error; one finite amount of CPU work per call. */
int usb_transport_step(UsbTransport *);
bool usb_transport_quiescent(const UsbTransport *);
void usb_transport_cancel(UsbTransport *);
#endif
