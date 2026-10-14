#ifndef NDVIDEO_ARM926_CPU_RAM_SPAN_H
#define NDVIDEO_ARM926_CPU_RAM_SPAN_H
#include "arm926_ram_span.h"

/* CPU-only reads may use native stack aliases. Validate each translation to
 * DRAM and its privileged access; never use this to authorize DMA addresses.
 * The caller authenticates/owns the active L1 table, as for ram_span(). */
static inline bool arm926_cpu_ram_span(uint32_t at,uint32_t bytes,uint32_t l1,
                                      Arm926ReadWord read_word,void *context)
{
    if(!read_word||(at&3U)||!bytes||bytes>0x4000000U||at>UINT32_MAX-bytes)return false;
    uint32_t end=at+bytes;
    while(at<end){
        uint32_t entry=read_word(l1+(at>>20)*4U,context);
        if(entry&0x1e0U)return false;
        uint32_t physical,mask,permission_shift=0,page=0;
        if((entry&3U)==2U){
            if(!(entry&0xc00U))return false;
            mask=0xfff00000U;physical=(entry&mask)|(at&~mask);
        } else if((entry&3U)==1U){
            uint32_t table=entry&0xfffffc00U;
            if(!((table>=0x10000000U&&table<=0x13fffc00U)||
                 (table>=0xa4000000U&&table<=0xa400fc00U)))return false;
            uint32_t backing=read_word(l1+(table>>20)*4U,context);
            if((backing&0x1e3U)!=2U||!(backing&0xc00U)||
                (backing&0xfff00000U)!=(table&0xfff00000U))return false;
            uint32_t index=(at>>12)&255U;
            page=read_word(table+index*4U,context);
            if((page&3U)==2U){mask=0xfffff000U;permission_shift=10;}
            else if((page&3U)==1U){
                if(page&0xf000U)return false;
                for(unsigned i=index&~15U;i<(index&~15U)+16U;++i)
                    if(read_word(table+i*4U,context)!=page)return false;
                mask=0xffff0000U;permission_shift=14;
            } else return false;
            if(!((page>>(4U+2U*((at>>permission_shift)&3U)))&3U))return false;
            physical=(page&mask)|(at&~mask);
        } else return false;
        /* Stop at AP subpage boundaries as well as section/page boundaries. */
        uint32_t unit=permission_shift?1U<<permission_shift:0x100000U;
        uint64_t next=((uint64_t)at&~((uint64_t)unit-1))+unit;
        uint32_t amount=next>end?end-at:(uint32_t)(next-at);
        if(physical<0x10000000U||physical>0x14000000U-amount)return false;
        at+=amount;
    }
    return true;
}
#endif
