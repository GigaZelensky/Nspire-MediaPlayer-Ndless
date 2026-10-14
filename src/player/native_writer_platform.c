#include "native_writer_platform.h"
#include "native_interrupts.h"
#include "../platform/portable_reader_platform.h"
#include <os.h>

static PortableReaderPlatform mapping;
static NativeWriterApi api;
static bool attempted, supported;
static const char *stage = "not requested";

#define EXPORT(name, number) \
    static uint32_t export_##name(void) { \
        unsigned mask = native_interrupt_mask(); \
        register uint32_t result __asm__("r0"); \
        __asm__ volatile("swi %[n]" : "=r"(result) \
            : [n] "i"(__SYSCALLS_ISVAR | (number)) \
            : "r1", "r2", "r3", "r4", "r12", "lr", "memory", "cc"); \
        uint32_t value = result; \
        native_critical_leave(mask); \
        return value; \
    }
EXPORT(fopen, e_fopen)
EXPORT(fwrite, e_fwrite)
EXPORT(fclose, e_fclose)
EXPORT(remove, e_remove)
EXPORT(stat, e_stat)
EXPORT(errno_addr, e_errno_addr)
EXPORT(task, e_TCC_Current_Task_Pointer)

typedef struct { void (*tick)(void *); void *context; } WriterProgress;
static bool writer_progress(void *context)
{
    WriterProgress *progress = context;
    progress->tick(progress->context);
    native_critical_enter();
    return portable_reader_platform_refresh(&mapping);
}
const NativeWriterApi *native_writer_platform_prepare(void (*tick)(void *), void *context)
{
    unsigned mask = native_critical_enter();
    if (!attempted) {
        attempted = true;
        stage = "hardware";
        if (*(const volatile uint32_t *)0x900a0000U == 0x202U) {
            uint32_t anchors[WR_EXPORTS] = {export_fopen(), export_fwrite(), export_fclose(),
                export_remove(), export_stat(), export_errno_addr(), export_task()};
            stage = "mapping";
            if (portable_reader_platform_refresh(&mapping)) {
                stage = "code and driver layout";
                WriterProgress progress = {tick, context};
                supported = native_writer_resolve_progress(portable_reader_platform_view(&mapping), anchors, &api,
                    tick ? writer_progress : NULL, &progress);
                if (supported) stage = "ready";
            }
        }
    }
    native_critical_leave(mask);
    return supported ? &api : NULL;
}
const NativeWriterApi *native_writer_platform(void)
{
    return native_writer_platform_prepare(NULL, NULL);
}
bool native_writer_span(uint32_t address, uint32_t bytes)
{
    unsigned mask = native_critical_enter();
    bool valid = !(address & 3U) && portable_reader_platform_refresh(&mapping);
    const PortableView *view = valid ? portable_reader_platform_view(&mapping) : NULL;
    valid = view && view->allow_span(view->context, address, bytes, PORTABLE_DATA);
    native_critical_leave(mask);
    return valid;
}
void native_writer_debug(FILE *file)
{
    if (!file) return;
    fprintf(file, "native_writer supported=%u stage=%s task_global=%08lx policy=%08lx spi_global=%08lx callback=%08lx\n",
        supported, stage, (unsigned long)api.task_global, (unsigned long)api.interrupt_policy,
        (unsigned long)api.spi_global, (unsigned long)api.spi_callback);
}
