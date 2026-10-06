#include "usb_fs_worker.h"
#include <string.h>
extern void private_context_swap(uint32_t **, uint32_t *);
extern void private_context_entry(void);
void usb_fs_context_init(UsbFsContext *c, void *stack, size_t bytes, void (*entry)(void *),
                         void *argument)
{
    uint32_t *frame = (uint32_t *)(((uintptr_t)stack + bytes) & ~(uintptr_t)7U) - 10;
    memset(frame, 0, 10 * sizeof(*frame));
    frame[0] = (uint32_t)(uintptr_t)argument;
    frame[1] = (uint32_t)(uintptr_t)entry;
    __asm__ volatile("mov %0,r9" : "=r"(frame[5]));
    frame[9] = (uint32_t)(uintptr_t)private_context_entry;
    c->foreground = NULL;
    c->background = frame;
}
void usb_fs_context_resume(UsbFsContext *c)
{
    private_context_swap((uint32_t **)&c->foreground, c->background);
}
void usb_fs_context_yield(UsbFsContext *c)
{
    private_context_swap((uint32_t **)&c->background, c->foreground);
}
void usb_fs_context_destroy(UsbFsContext *c)
{
    c->foreground = c->background = NULL;
}
