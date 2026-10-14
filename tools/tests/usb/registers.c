#include "usb_registers.h"
#include <assert.h>
#include <stdio.h>
static uint32_t saved_model;
uint32_t nspire_hardware_id(void) { return saved_model; }
int main(void)
{
    UsbRegisters r;
    /* No live MMIO access: model selection still works with the peripheral
     * bus gated, using the validated identity captured at application entry. */
    saved_model = 0x202;
    assert(usb_registers_current(&r) && !r.classic);
    saved_model = 0x101;
    assert(usb_registers_current(&r) && r.classic);
    saved_model = 0;
    assert(!usb_registers_current(&r));
    const unsigned logical[] = {0x10, 0x14, 0x18, 0x28, 0x30};
    const uint32_t cx[] = {0xb0000140, 0xb0000144, 0xb0000148, 0xb0000158, 0xb0000184};
    const uint32_t cx2[] = {0xb0000010, 0xb0000014, 0xb0000018, 0xb0000028, 0xb0000030};
    for (unsigned alias = 0; alias < 2; ++alias) {
        assert(usb_registers_for_asic(alias ? 0x10000101 : 0x101, &r));
        assert(r.classic && r.capability == 0xb0000100 && r.otg == 0xb00001a4 && r.role_mask == 256);
        assert(usb_registers_storage_speed(&r, 2) && usb_registers_storage_speed(&r, 3));
        assert(!usb_registers_storage_speed(&r, 0) && !usb_registers_storage_speed(&r, 1));
        for (unsigned i = 0; i < 5; ++i) assert(usb_register_address(&r, logical[i]) == cx[i]);
    }
    assert(usb_registers_for_asic(0x202, &r));
    assert(!r.classic && r.capability == 0xb0000000 && r.role_mask == (1U << 21));
    assert(usb_registers_storage_speed(&r, 3) && !usb_registers_storage_speed(&r, 2));
    for (unsigned i = 0; i < 5; ++i) assert(usb_register_address(&r, logical[i]) == cx2[i]);
    assert(!usb_registers_for_asic(0xdeadbeef, &r) && !r.capability && !r.command);
    assert(!usb_registers_storage_speed(&r, 3) && !usb_registers_storage_speed(NULL, 3));
    puts("PASS CX/CX II register layouts, CX identification aliases and unknown hardware rejection");
}
