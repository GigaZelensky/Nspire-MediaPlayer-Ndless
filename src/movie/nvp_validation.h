#ifndef NDVIDEO_NVP_VALIDATION_H
#define NDVIDEO_NVP_VALIDATION_H

#include <stdbool.h>
#include <stddef.h>
#include "nvp_format.h"

/* Validate on-disk ranges before allocating or decoding their contents. */
unsigned nvp_header_wire_bytes(unsigned version);
unsigned nvp_index_wire_bytes(unsigned version);
bool nvp_header_parse(MovieHeader *header, const void *data, size_t bytes);
void nvp_index_parse(ChunkIndexEntry *entry, const void *data, unsigned version);
bool nvp_header_is_valid(const MovieHeader *header, uint64_t file_size);
bool nvp_index_is_valid(const MovieHeader *header, const ChunkIndexEntry *index);

#endif
