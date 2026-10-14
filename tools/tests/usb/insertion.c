#define _GNU_SOURCE
#include "usb_autoconnect.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#undef interface
#else
#include <sys/mman.h>
#endif
#define WORD(a) (*(volatile uint32_t *)(uintptr_t)(a))
static uint32_t model;
static unsigned cpu_mask = 0x80;
static unsigned native_critical_enter(void) { unsigned old = cpu_mask; cpu_mask = 0xc0; return old; }
static void native_critical_leave(unsigned old) { cpu_mask = old; }
static void test_barrier(void) { assert(cpu_mask == 0xc0); }
static UsbAutoConnect auto_connection;
static struct { uint32_t cap, identity, otg, port, samples, unknown, insertions; unsigned presence; } detection;
uint32_t nspire_hardware_id(void) { return model; }
static bool usb_storage_mounted(void) { return false; }
/* PRODUCTION_SAMPLE */
/* PRODUCTION_POLL */
static void map_page(uint32_t address)
{
#ifdef _WIN32
    void *mapped = VirtualAlloc((void *)(uintptr_t)address, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void *mapped = mmap((void *)(uintptr_t)address, 4096, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(mapped == (void *)(uintptr_t)address);
    memset(mapped, 0, 4096);
}
int main(void)
{
    map_page(0x900b0000); map_page(0x90140000); map_page(0xb0000000); map_page(0x900c0000);
    WORD(0x900b0018) = WORD(0x90140050) = 0xffffffff;
    WORD(0x90140060) = 0xffebffff;
    for (unsigned classic = 0; classic < 2; ++classic) {
        model = classic ? 0x101 : 0x202;
        UsbRegisters layout, sampled;
        uint32_t cap, identity, otg, port;
        assert(usb_registers_current(&layout));
        WORD(layout.capability) = classic ? 0x01000040 : 0x01000010;
        if (classic) WORD(0xb0000000) = 0xe241fa05;
        WORD(layout.otg) = 0; WORD(layout.port) = 0;
        assert(sample_usb_registers(&sampled, &cap, &identity, &otg, &port));
        assert(usb_auto_connect_presence(&sampled, cap, identity, otg, port) == USB_AUTO_ADAPTER);
        auto_connection = (UsbAutoConnect){0};
        assert(!usb_browser_auto_pending(10));
        assert(usb_browser_auto_pending(260));
        WORD(layout.otg) = layout.role_mask;
        assert(sample_usb_registers(&sampled, &cap, &identity, &otg, &port));
        assert(usb_auto_connect_presence(&sampled, cap, identity, otg, port) == 0);
    }
    model = 0x202;
    WORD(0xb0000000) = WORD(0xb0000080) = WORD(0xb0000030) = 0;
    for (unsigned now = 0; now < 1000; now += 10) assert(!usb_browser_auto_pending(now));
    assert(WORD(0x900b0018) == 0xffffffff && WORD(0x90140050) == 0xffffffff && WORD(0x90140060) == 0xffebffff);
    model = 0;
    UsbRegisters r; uint32_t cap, identity, otg, port;
    assert(!sample_usb_registers(&r, &cap, &identity, &otg, &port));
    assert(cpu_mask == 0x80);
    puts("PASS bounded USB event poll: model layouts, adapter debounce, device-role rejection, stopped-clock bounds and restored power/mask; no native setup or sleep dependency");
    return 0;
}
