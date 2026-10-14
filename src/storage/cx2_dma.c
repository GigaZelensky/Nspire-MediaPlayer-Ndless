#include "cx2_dma.h"
#include <string.h>
static uint32_t rd(Cx2DmaLease *d,uint32_t a){return d->bus.read(d->bus.context,a);}
static void wr(Cx2DmaLease *d,uint32_t a,uint32_t v){d->bus.write(d->bus.context,a,v);}
static bool ram(uint32_t a,uint32_t n){return a>=0x10000000U && n && n<=4096 && a<=0x14000000U-n;}
bool cx2_dma_idle(Cx2DmaLease *d){
    return d && d->claimed && !(rd(d,CX2_DMAC_BASE+0x1c)&d->bit) &&
        !(rd(d,CX2_DMAC_BASE+0x20)&d->bit) && !(rd(d,d->base)&1U) &&
        !(rd(d,d->base+4)&0x100U);
}
bool cx2_dma_claim(Cx2DmaLease *d,Cx2DmaBus bus){
    if(!d || !bus.read || !bus.write)return false;
    memset(d,0,sizeof(*d));d->bus=bus;
    uint32_t revision=rd(d,CX2_DMAC_BASE+0x30),features=rd(d,CX2_DMAC_BASE+0x34);
    unsigned count=(features>>12)&15U;
    if((revision>>16)!=1 || count<2 || count>8 || !(features&0x200U))return false;
    d->saved_global=rd(d,CX2_DMAC_BASE+0x24);
    if(d->saved_global&~1U)return false; /* Little-endian, recognized configuration only. */
    uint32_t pending=rd(d,CX2_DMAC_BASE+0x14),errors=rd(d,CX2_DMAC_BASE+0x18);
    uint32_t unavailable=rd(d,CX2_DMAC_BASE+0x1c)|rd(d,CX2_DMAC_BASE+0x20)|pending|errors|(errors>>16);
    for(unsigned i=count-1;i>0;--i){
        if(unavailable&(1U<<i))continue;
        uint32_t base=CX2_DMAC_BASE+0x100+32*i;
        for(unsigned j=0;j<6;++j){
            d->saved[j]=rd(d,base+4*j);
        }
        /* Disabled channels may retain reset defaults or an old descriptor.
         * Borrow only under IRQ/FIQ exclusion and restore all registers before
         * returning to the OS. Never steal enabled/busy channels, an abort in
         * progress, or a pending completion/error (checked above). */
        if((d->saved[0]&0x8001U) || (d->saved[1]&0x100U))continue;
        d->channel=i;d->base=base;d->bit=1U<<i;d->claimed=true;
        if(!(d->saved_global&1U)){
            if(rd(d,CX2_DMAC_BASE+0x1c)||rd(d,CX2_DMAC_BASE+0x20)){d->claimed=false;return false;}
            wr(d,CX2_DMAC_BASE+0x24,1);d->enabled_global=true;
        }
        return true;
    }
    return false;
}
bool cx2_dma_start(Cx2DmaLease *d,uint32_t src,uint32_t dst,uint32_t bytes,int request){
    if(!d || !d->claimed || !cx2_dma_idle(d) || request < -1 || request>15 ||
       !bytes || bytes>4096U || (bytes&3U) || (dst&31U) || !ram(dst,bytes))return false;
    if(request<0){
        if((src&3U)||!ram(src,bytes)||(src<dst+bytes && dst<src+bytes))return false;
    } else if(src!=CX2_NAND_FIFO)return false;
    d->remaining=bytes/4;d->errors=0;
    wr(d,CX2_DMAC_BASE+0x08,d->bit);
    wr(d,CX2_DMAC_BASE+0x10,d->bit|(d->bit<<16));
    wr(d,d->base+4,7U|(request<0?0U:0x80U|((uint32_t)request<<3)));
    wr(d,d->base+8,src);wr(d,d->base+12,dst);wr(d,d->base+16,0);
    wr(d,d->base+20,bytes/4);
    /* Both endpoints on AHB1, 32-bit single-beat transfers, no linked list.
     * Peripheral reads have fixed source and incrementing RAM destination.
     * CFG masks IRQs but leaves raw terminal/error status observable. */
    uint32_t control=6U|(2U<<11)|(2U<<8);
    if(request>=0)control|=0x80U|(2U<<5);
    d->started=true;wr(d,d->base,control|1U);
    return true;
}
int cx2_dma_poll(Cx2DmaLease *d){
    if(!d || !d->claimed || !d->started)return CX2_DMA_ERROR;
    d->remaining=rd(d,d->base+20);
    d->errors=rd(d,CX2_DMAC_BASE+0x18)&(d->bit|(d->bit<<16));
    if(d->errors)return CX2_DMA_ERROR;
    if((rd(d,CX2_DMAC_BASE+0x14)&d->bit) && !d->remaining &&
       !(rd(d,CX2_DMAC_BASE+0x20)&d->bit) && !(rd(d,d->base+4)&0x100U)){
        uint32_t control=rd(d,d->base);
        if(control&1U)wr(d,d->base,control&~1U);
        if(cx2_dma_idle(d))return CX2_DMA_DONE;
    }
    return CX2_DMA_PENDING;
}
void cx2_dma_abort(Cx2DmaLease *d){
    if(!d || !d->claimed || !d->started)return;
    wr(d,d->base+4,rd(d,d->base+4)|7U);
    wr(d,d->base,(rd(d,d->base)&~1U)|0x8000U);
}
bool cx2_dma_release(Cx2DmaLease *d){
    if(!d || !d->claimed || !cx2_dma_idle(d))return false;
    wr(d,CX2_DMAC_BASE+0x08,d->bit);wr(d,CX2_DMAC_BASE+0x10,d->bit|(d->bit<<16));
    /* The saved channel was idle. Restore its disabled control word last. */
    for(unsigned j=1;j<6;++j)wr(d,d->base+4*j,d->saved[j]);
    wr(d,d->base,d->saved[0]);
    if(d->enabled_global){
        if(rd(d,CX2_DMAC_BASE+0x1c)||rd(d,CX2_DMAC_BASE+0x20))return false;
        wr(d,CX2_DMAC_BASE+0x24,d->saved_global);
    }
    d->claimed=false;return true;
}
