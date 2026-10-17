#include "player/native_usb_ui.h"
#include "player/native_interrupts.h"
#include "platform/portable_reader_platform.h"
#include "platform/nspire_hardware.h"
#include <stddef.h>
bool audio_platform_cx2(void) {return nspire_is_cx2();}
uint32_t audio_native_usb_irq_mask(void)
{
    return native_usb_ui_bus_irq_mask();
}
bool audio_dma_valid(void *address, size_t bytes)
{
    unsigned mask = native_critical_enter();
    PortableReaderPlatform mapping = {0};
    bool valid = bytes <= UINT32_MAX && portable_reader_platform_refresh(&mapping);
    const PortableView *view = valid ? portable_reader_platform_view(&mapping) : NULL;
    valid = view && view->allow_span(view->context, (uint32_t)(uintptr_t)address, (uint32_t)bytes,
                                     PORTABLE_DATA);
    native_critical_leave(mask);
    return valid;
}
