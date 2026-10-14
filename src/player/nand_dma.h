#ifndef NDVIDEO_PLAYER_NAND_DMA_H
#define NDVIDEO_PLAYER_NAND_DMA_H
#include "../platform/portable_reader_platform.h"
#include "../storage/cx2_dma.h"
typedef struct {
    uint32_t started, completed, bytes, polls, fallbacks, small_reads;
    uint32_t mapping_rejected, failures, cancellations, last_error, last_remaining, channel;
    uint32_t native_context, busy;
    bool disabled;
} PlayerNandDmaStats;
typedef struct {
    PortableReaderPlatform *platform;
    void *buffer;
    uint32_t capacity, cache_bytes, bytes;
    Cx2DmaLease lease;
    PlayerNandDmaStats stats;
    bool active, allow_start, setup_failed;
} PlayerNandDma;
SpiNandDma player_nand_dma_bind(PlayerNandDma *, PortableReaderPlatform *, void *, uint32_t);
#endif
