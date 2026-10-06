
#include "usb_transport.h"
#include "usb_scsi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum { CMD = 0x10 / 4, STATUS = 0x14 / 4, IMASK = 0x18 / 4, LIST = 0x28 / 4, PORT = 0x30 / 4 };
static struct {
    uint32_t reg[16], clock, tag, clean, invalid, barriers;
    unsigned stage, stuck_native, stuck_private, stuck_force, never_complete, bad_csw, short_data,
        disconnect, token_error;
    bool private_dma;
} sim;
static unsigned char bounce[4096];
static UsbQueueHead queue;
static UsbQtd descriptor;
static UsbTransport transport;
static void put(unsigned char *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (unsigned char)(n >> (8 * i));
}
static uint32_t get(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t readreg(void *unused, unsigned offset)
{
    (void)unused;
    if (offset == 0x14 && sim.private_dma && !sim.never_complete && (descriptor.token & 0x80)) {
        unsigned bytes = (descriptor.token >> 16) & 0x7fff;
        bool input = (descriptor.token & 0x300) == 0x100;
        if (!input && bytes == 31) {
            assert(get(bounce) == 0x43425355);
            sim.tag = get(bounce + 4);
            sim.stage = 1;
        } else if (input && bytes == 13) {
            put(bounce, sim.bad_csw == 1 ? 0 : 0x53425355);
            put(bounce + 4, sim.tag + (sim.bad_csw == 2));
            put(bounce + 8, sim.bad_csw == 3 ? 1 : 0);
            bounce[12] = sim.bad_csw == 4 ? 2 : sim.bad_csw == 5 ? 1 : 0;
        } else if (input) {
            for (unsigned i = 0; i < bytes; ++i)
                bounce[i] = (unsigned char)(i * 17 + 3);
            sim.stage = 2;
        }
        descriptor.token = (descriptor.token & 0x80000000) ^ 0x80000000;
        if (sim.short_data && input && bytes != 13)
            descriptor.token |= 1U << 16;
        if (sim.token_error)
            descriptor.token |= sim.token_error;
        sim.private_dma = false;
        sim.reg[STATUS] |= 1;
    }
    if (sim.disconnect)
        sim.reg[PORT] = 0;
    return sim.reg[offset / 4];
}
static void writereg(void *unused, unsigned offset, uint32_t value)
{
    (void)unused;
    if (offset == 0x14) {
        sim.reg[STATUS] &= ~value;
        return;
    }
    sim.reg[offset / 4] = value;
    if (offset == 0x10) {
        if (!(value & 1)) {
            if (!sim.stuck_force) {
                sim.reg[STATUS] |= 4096;
                sim.reg[STATUS] &= ~32768U;
            }
            return;
        }
        if (value & 32) {
            sim.reg[STATUS] |= 32768;
            if (sim.reg[LIST] == 0x10002000)
                sim.private_dma = true;
        } else if (!(sim.reg[LIST] == 0x10002000 ? sim.stuck_private : sim.stuck_native))
            sim.reg[STATUS] &= ~32768U;
    }
}
static uint32_t tick(void *u)
{
    (void)u;
    return ++sim.clock;
}
static void clean(void *u, void *p, size_t n)
{
    (void)u;
    (void)p;
    (void)n;
    ++sim.clean;
}
static void invalid(void *u, void *p, size_t n)
{
    (void)u;
    (void)p;
    (void)n;
    ++sim.invalid;
}
static void barrier(void *u)
{
    (void)u;
    ++sim.barriers;
}
static void init(void)
{
    memset(&sim, 0, sizeof(sim));
    sim.reg[CMD] = 0x80021;
    sim.reg[STATUS] = 32768;
    sim.reg[IMASK] = 0x17;
    sim.reg[LIST] = 0x12340000;
    sim.reg[PORT] = 5;
    UsbBus bus = {NULL, readreg, writereg, tick, clean, invalid, barrier};
    usb_transport_init(&transport, bus, bounce, &queue, &descriptor, 0x10001000, 0x10002000,
                       0x10002040, 32768);
    transport.address = 7;
    transport.endpoint_in = 0x81;
    transport.endpoint_out = 2;
}
static int finish(void)
{
    int result = 0;
    for (unsigned i = 0; i < 70000 && !result; ++i)
        result = usb_transport_step(&transport);
    assert(result);
    return result;
}
static void restored(void)
{
    assert(!transport.owns_schedule && sim.reg[LIST] == 0x12340000 && sim.reg[CMD] == 0x80021 &&
           sim.reg[IMASK] == 0x17);
}
int main(void)
{
    unsigned char data[4096];
    unsigned cases = 0;
    for (unsigned input = 0; input < 2; ++input)
        for (unsigned pending = 0; pending < 4; ++pending) {
            init();
            sim.reg[STATUS] |= pending;
            assert(usb_transport_begin(&transport, input, data, sizeof(data)));
            assert(finish() == 1);
            assert(transport.actual == 4096);
            restored();
            assert((sim.reg[STATUS] & 3) == pending);
            assert(sim.clean && sim.invalid && sim.barriers);
            ++cases;
        }
    init();
    assert(usb_transport_begin(&transport, true, NULL, 16));
    while (transport.phase != USB_TRANSFER_WAIT)
        assert(!usb_transport_step(&transport));
    sim.clock += 200000;
    assert(finish() == 1);
    restored();
    ++cases;
    init();
    assert(!usb_transport_begin(&transport, true, NULL, 4097));
    assert(!usb_transport_begin(&transport, false, NULL, 1));
    assert(!usb_transport_begin(&transport, true, NULL, 0));
    ++cases;
    init();
    sim.stuck_native = 1;
    assert(usb_transport_begin(&transport, true, NULL, 16));
    assert(finish() == -1);
    assert(transport.error == USB_TRANSPORT_TIMEOUT);
    restored();
    ++cases;
    init();
    sim.never_complete = 1;
    assert(usb_transport_begin(&transport, true, NULL, 16));
    assert(finish() == -1);
    assert(transport.error == USB_TRANSPORT_TIMEOUT);
    restored();
    ++cases;
    for (unsigned force = 0; force < 2; ++force) {
        init();
        sim.stuck_private = 1;
        sim.stuck_force = force;
        assert(usb_transport_begin(&transport, true, NULL, 16));
        assert(finish() == -1 && transport.poisoned);
        assert(transport.error == USB_TRANSPORT_UNSAFE_STOP);
        assert(!usb_transport_begin(&transport, true, NULL, 16));
        assert(transport.owns_schedule == !!force);
        ++cases;
    }
    for (unsigned token = 4; token <= 64; token *= 2) {
        init();
        sim.token_error = token;
        assert(usb_transport_begin(&transport, true, NULL, 16));
        assert(finish() == -1);
        restored();
        ++cases;
    }
    init();
    assert(usb_transport_begin(&transport, true, NULL, 16));
    usb_transport_cancel(&transport);
    assert(finish() == -1);
    restored();
    ++cases;
    init();
    assert(usb_transport_begin(&transport, true, NULL, 16));
    sim.disconnect = 1;
    assert(finish() == -1 && transport.error == USB_TRANSPORT_DISCONNECTED);
    restored();
    ++cases;
    for (unsigned bad = 0; bad <= 6; ++bad) {
        init();
        UsbScsi scsi;
        usb_scsi_init(&scsi, &transport);
        sim.bad_csw = bad <= 5 ? bad : 0;
        sim.short_data = bad == 6;
        unsigned char command[10] = {0x28};
        assert(usb_scsi_begin(&scsi, command, 10, data, sizeof(data), true));
        int result = 0;
        for (unsigned i = 0; i < 1000 && !result; ++i)
            result = usb_scsi_step(&scsi);
        assert(result == (bad ? -1 : 1));
        assert(!usb_scsi_busy(&scsi));
        restored();
        if (!bad)
            for (unsigned i = 0; i < sizeof(data); ++i)
                assert(data[i] == (unsigned char)(i * 17 + 3));
        ++cases;
    }
    printf("PASS %u cases: DMA sequencing, cache handoff, interrupt preservation, bounded "
           "timeouts, disconnect/cancel, poisoned ownership, BOT integrity\n",
           cases);
    return 0;
}
