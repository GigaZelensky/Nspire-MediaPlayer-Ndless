#ifndef NDVIDEO_NATIVE_WRITER_PLATFORM_H
#define NDVIDEO_NATIVE_WRITER_PLATFORM_H
#include "native_writer_resolver.h"
#include <stdio.h>
const NativeWriterApi *native_writer_platform(void);
/* Read-only preparation before playback; no files are opened or written. */
const NativeWriterApi *native_writer_platform_prepare(void (*)(void *), void *);
bool native_writer_span(uint32_t address, uint32_t bytes);
void native_writer_debug(FILE *file);
#endif
