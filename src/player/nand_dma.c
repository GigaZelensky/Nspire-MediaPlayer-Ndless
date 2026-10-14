#include "nand_dma.h"
#include "native_interrupts.h"
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
static void drain(void)
{
#if defined(__arm__)
    __asm__ volatile("mcr p15,0,%0,c7,c10,4"::"r"(0):"memory");
#endif
}
static uint32_t read_reg(void *unused,uint32_t address)
{
    (void)unused;return REG(address);
}
static void write_reg(void *unused,uint32_t address,uint32_t value)
{
    (void)unused;REG(address)=value;drain();
}
static void prepare_cache(void *buffer,uint32_t bytes)
{
#if defined(__arm__)
    for(uintptr_t p=(uintptr_t)buffer;p<(uintptr_t)buffer+bytes;p+=32U)
        __asm__ volatile("mcr p15,0,%0,c7,c14,1"::"r"(p):"memory");
#else
    (void)buffer;(void)bytes;
#endif
    drain();
}
static void finish_cache(void *buffer,uint32_t bytes)
{
    drain();
#if defined(__arm__)
    for(uintptr_t p=(uintptr_t)buffer;p<(uintptr_t)buffer+bytes;p+=32U)
        __asm__ volatile("mcr p15,0,%0,c7,c6,1"::"r"(p):"memory");
#else
    (void)buffer;(void)bytes;
#endif
    drain();
}
static bool device_mapping(const PortableReaderPlatform *p,uint32_t address)
{
    uint32_t descriptor=REG(p->table_alias+(address>>20)*4U);
    return (descriptor&0x1ebU)==2U && (descriptor&0xfff00000U)==(address&0xfff00000U) &&
        (descriptor&0xc00U);
}
static int begin(void *context,void *destination,uint32_t bytes)
{
    PlayerNandDma *d=context;
    if(bytes<128U){++d->stats.small_reads;return false;}
    if(d->stats.disabled)return -1;
    if(!d->allow_start){++d->stats.fallbacks;++d->stats.native_context;return 0;}
    PortableReaderPlatform *p=d->platform;
    uint32_t cache_bytes=(bytes+31U)&~31U;
    if(d->active || !p || p->asic!=0x202U || p->kind!=NAND_PAGE_CX2_SPI ||
       !p->table_valid || native_interrupt_mask()!=0xc0U || destination!=d->buffer ||
       ((uintptr_t)destination&31U) || !bytes || bytes>2112U || cache_bytes>d->capacity ||
       !p->view.allow_span(p->view.context,(uint32_t)(uintptr_t)destination,cache_bytes,PORTABLE_DATA) ||
       !device_mapping(p,CX2_DMAC_BASE) || !device_mapping(p,0xb8000000U)){
        ++d->stats.mapping_rejected;d->stats.disabled=true;return -1;
    }
    /* Playback may present through the OS's LCD DMA channel while we decode.
     * Borrow a channel only on an already-enabled controller, so this lease
     * never changes the global enable beneath another peripheral user. */
    if(REG(CX2_DMAC_BASE+0x24)!=1U ||
       !cx2_dma_claim(&d->lease,(Cx2DmaBus){NULL,read_reg,write_reg})){
        ++d->stats.fallbacks;++d->stats.busy;return 0;
    }
    d->cache_bytes=cache_bytes;d->bytes=bytes;d->setup_failed=false;
    prepare_cache(destination,cache_bytes);
    d->active=true;
    if(!cx2_dma_start(&d->lease,CX2_NAND_FIFO,(uint32_t)(uintptr_t)destination,(bytes+3U)&~3U,0)){
        finish_cache(destination,cache_bytes);
        if(cx2_dma_release(&d->lease)){
            d->active=false;++d->stats.failures;d->stats.disabled=true;return -1;
        }
        /* Report ownership to the engine so its cleanup cannot fall through
         * to PIO while a lease still exists. */
        d->setup_failed=true;
    }
    d->stats.channel=d->lease.channel;
    ++d->stats.started;return true;
}
static int poll(void *context)
{
    PlayerNandDma *d=context;++d->stats.polls;
    if(d->setup_failed)return -1;
    int result=cx2_dma_poll(&d->lease);
    d->stats.last_error=d->lease.errors;d->stats.last_remaining=d->lease.remaining;
    return result==CX2_DMA_DONE?1:result==CX2_DMA_ERROR?-1:0;
}
static void abort_read(void *context,bool failed)
{
    PlayerNandDma *d=context;
    if(failed){++d->stats.failures;d->stats.disabled=true;}
    else ++d->stats.cancellations;
    if(!cx2_dma_idle(&d->lease))cx2_dma_abort(&d->lease);
}
static bool release(void *context,bool completed)
{
    PlayerNandDma *d=context;
    if(!d->active)return true;
    if(!cx2_dma_idle(&d->lease))return false;
    finish_cache(d->buffer,d->cache_bytes);
    if(!cx2_dma_release(&d->lease))return false;
    d->active=false;
    if(completed){++d->stats.completed;d->stats.bytes+=d->bytes;}
    return true;
}
SpiNandDma player_nand_dma_bind(PlayerNandDma *d,PortableReaderPlatform *p,void *buffer,uint32_t capacity)
{
    if(!d || d->active || !p || p->kind!=NAND_PAGE_CX2_SPI)return (SpiNandDma){0};
    d->platform=p;d->buffer=buffer;d->capacity=capacity;
    return (SpiNandDma){d,begin,poll,abort_read,release};
}
