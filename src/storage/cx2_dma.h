#ifndef NDVIDEO_CX2_DMA_H
#define NDVIDEO_CX2_DMA_H
#include <stdbool.h>
#include <stdint.h>
#define CX2_DMAC_BASE 0xbc000000U
#define CX2_NAND_FIFO 0xb8000100U
typedef struct {
    void *context;
    uint32_t (*read)(void *, uint32_t);
    void (*write)(void *, uint32_t, uint32_t);
} Cx2DmaBus;
typedef struct {
    Cx2DmaBus bus;
    uint32_t channel, base, bit, saved_global, saved[6], remaining, errors;
    bool claimed, started, enabled_global;
} Cx2DmaLease;
enum { CX2_DMA_PENDING, CX2_DMA_DONE, CX2_DMA_ERROR };
/* Caller owns IRQ/FIQ exclusion and has authenticated CX II MMIO and buffer
 * identity mappings. Cache maintenance belongs to the caller. Channel zero is
 * reserved for the OS LCD path. Other channels must be idle without pending
 * IRQ status. Their saved descriptors are restored before exclusion ends. */
bool cx2_dma_claim(Cx2DmaLease *, Cx2DmaBus);
bool cx2_dma_start(Cx2DmaLease *, uint32_t source, uint32_t destination, uint32_t bytes, int request);
int cx2_dma_poll(Cx2DmaLease *);
void cx2_dma_abort(Cx2DmaLease *);
bool cx2_dma_idle(Cx2DmaLease *);
bool cx2_dma_release(Cx2DmaLease *);
#endif
