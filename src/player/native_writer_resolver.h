#ifndef NDVIDEO_NATIVE_WRITER_RESOLVER_H
#define NDVIDEO_NATIVE_WRITER_RESOLVER_H
#include "../platform/portable_resolver.h"

enum { WR_OPEN, WR_WRITE, WR_CLOSE, WR_REMOVE, WR_STAT, WR_ERRNO, WR_TASK, WR_EXPORTS };
typedef struct {
    uint32_t function[WR_EXPORTS];
    uint32_t task_global, interrupt_policy, spi_global;
    uint32_t spi_control, spi_callback, spi_context;
} NativeWriterApi;

/* Read-only discovery. Direct file entry points come from Ndless; all code,
 * descriptor and kernel-layout witnesses are checked before returning bindings. */
bool native_writer_resolve(const PortableView *, const uint32_t anchors[WR_EXPORTS],
                           NativeWriterApi *);
bool native_writer_resolve_progress(const PortableView *, const uint32_t anchors[WR_EXPORTS],
                                    NativeWriterApi *, bool (*)(void *), void *);
#endif
