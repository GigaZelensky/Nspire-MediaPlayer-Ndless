#include "arm926_cpu_ram_span.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t l1[4096],l2[256];
static uint32_t table=0x10004000U,coarse=0x10008000U;
static uint32_t read_word(uint32_t at,void *unused){(void)unused;
 if(at>=table&&at<table+sizeof(l1))return l1[(at-table)/4];
 assert(at>=coarse&&at<coarse+sizeof(l2));return l2[(at-coarse)/4];}
int main(void){
 for(unsigned i=0;i<4096;++i)l1[i]=(i<<20)|0x402;
 l1[0x180]=0x12000402;
 assert(arm926_cpu_ram_span(0x1800c1cc,32,table,read_word,NULL));
 assert(!arm926_ram_span(0x1800c1cc,32,table,read_word,NULL,NULL));
 l1[0x180]=0xb0000402;assert(!arm926_cpu_ram_span(0x1800c1cc,32,table,read_word,NULL));
 l1[0x180]=0x12000002;assert(!arm926_cpu_ram_span(0x1800c1cc,32,table,read_word,NULL));
 l1[0x180]=coarse|1;
 for(unsigned i=0;i<256;++i)l2[i]=(0x12000000+(i<<12))|0xff2;
 assert(arm926_cpu_ram_span(0x18000ffc,32,table,read_word,NULL));
 l2[1]&=~0x30U;assert(!arm926_cpu_ram_span(0x18000ffc,32,table,read_word,NULL));l2[1]|=0x30;
 l1[0x180]|=0x20;assert(!arm926_cpu_ram_span(0x1800c1cc,32,table,read_word,NULL));l1[0x180]&=~0x20U;
 for(unsigned i=0;i<16;++i)l2[i]=0x12000ff1;
 assert(arm926_cpu_ram_span(0x1800c1cc,32,table,read_word,NULL));
 l2[0]^=0x10000;assert(!arm926_cpu_ram_span(0x1800c1cc,32,table,read_word,NULL));
 coarse=0xa4008000;l1[0x180]=coarse|1;for(unsigned i=0;i<256;++i)l2[i]=(0x12000000+(i<<12))|0xff2;
 assert(arm926_cpu_ram_span(0x1800c1cc,32,table,read_word,NULL));
 assert(!arm926_cpu_ram_span(0xfffffffc,32,table,read_word,NULL));
 puts("PASS CPU stack aliases: DRAM translations, section/small/large pages, permissions/domains, SRAM tables and unchanged strict DMA checks");
}
