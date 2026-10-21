#ifndef NMEDIA_HEADER_DIAGNOSTIC_H
#define NMEDIA_HEADER_DIAGNOSTIC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef NDMEDIA_HEADER_DIAGNOSTIC
#define NDMEDIA_HEADER_DIAGNOSTIC 0
#endif
#if NDMEDIA_HEADER_DIAGNOSTIC
#include <stdio.h>
#include "platform/portable_reader_platform.h"
#include "storage/raw_file_reader.h"
void header_diagnostic_begin(const char *path);
void header_diagnostic_storage(const PortableReaderPlatform *,const PortableStorageSnapshot *,
    const RawFileReader *,uint64_t offset,uint32_t bytes,int result,unsigned stage,bool ready,bool reused);
void header_diagnostic_result(const char *stage,uint64_t file_bytes,bool encrypted,
    const uint8_t *data,size_t received,size_t expected,int error,bool valid);
/* The failed movie has closed before the existing error-report path calls
 * these. Export the owned capture into that same checked report stream. */
void header_diagnostic_compare(const char *path);
void header_diagnostic_write(FILE *file);
#else
#define header_diagnostic_begin(...) ((void)0)
#define header_diagnostic_result(...) ((void)0)
#define header_diagnostic_compare(...) ((void)0)
#define header_diagnostic_write(...) ((void)0)
#endif
#endif
