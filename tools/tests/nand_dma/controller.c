#include "cx2_dma.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint32_t reg[128],writes,channel;bool abort_stuck,keep_enabled;} Model;
static uint32_t read_reg(void *ctx,uint32_t address){
    Model *m=ctx;assert(address>=CX2_DMAC_BASE && address<CX2_DMAC_BASE+sizeof m->reg && !(address&3));
    return m->reg[(address-CX2_DMAC_BASE)/4];
}
static void write_reg(void *ctx,uint32_t address,uint32_t value){
    Model *m=ctx;unsigned o=address-CX2_DMAC_BASE;assert(!(o&3) && o<sizeof m->reg);++m->writes;
    if(o==8){assert(value==(1U<<m->channel));m->reg[0x14/4]&=~value;return;}
    if(o==16){assert(value==((1U<<m->channel)|(1U<<(m->channel+16))));m->reg[0x18/4]&=~value;return;}
    if(o==0x24){assert(value<=1);m->reg[o/4]=value;return;}
    assert(o>=0x100+32*m->channel && o<=0x114+32*m->channel);
    assert(m->channel>0); /* The OS display channel must never be touched. */
    m->reg[o/4]=value;
    if(o==0x100+32*m->channel){
        if(value&0x8000){
            if(!m->abort_stuck){m->reg[0x1c/4]&=~(1U<<m->channel);m->reg[0x20/4]&=~(1U<<m->channel);m->reg[(o+4)/4]&=~0x100U;}
            m->reg[0x18/4]|=1U<<(m->channel+16);
        } else if(value&1){
            m->reg[0x1c/4]|=1U<<m->channel;m->reg[0x20/4]|=1U<<m->channel;m->reg[(o+4)/4]|=0x100;
        } else if(!(m->reg[0x20/4]&(1U<<m->channel)))m->reg[0x1c/4]&=~(1U<<m->channel);
    }
}
static Model model(void){Model m={0};m.reg[0x30/4]=0x11900;m.reg[0x34/4]=0x6303;m.reg[0x24/4]=1;m.channel=5;return m;}
static bool claim(Model *m,Cx2DmaLease *d){memset(d,0,sizeof(*d));return cx2_dma_claim(d,(Cx2DmaBus){m,read_reg,write_reg});}
static void complete(Model *m){
    unsigned base=0x100+32*m->channel;
    m->reg[(base+20)/4]=0;m->reg[0x20/4]&=~(1U<<m->channel);m->reg[(base+4)/4]&=~0x100U;
    if(!m->keep_enabled){m->reg[base/4]&=~1U;m->reg[0x1c/4]&=~(1U<<m->channel);}
    m->reg[0x14/4]|=1U<<m->channel;
}
int main(void){
    unsigned cases=0;Model m;Cx2DmaLease d;
    m=model();m.reg[0x30/4]=0;assert(!claim(&m,&d)&&!m.writes);++cases;
    m=model();m.reg[0x34/4]=0;assert(!claim(&m,&d)&&!m.writes);++cases;
    m=model();m.reg[0x24/4]=3;assert(!claim(&m,&d)&&!m.writes);++cases;
    m=model();m.reg[0x1c/4]=0x3e;assert(!claim(&m,&d)&&!m.writes);++cases;
    m=model();m.reg[0x20/4]=0x3e;assert(!claim(&m,&d)&&!m.writes);++cases;
    m=model();for(unsigned i=1;i<6;++i)m.reg[(0x100+i*32)/4]=0x8000;assert(!claim(&m,&d)&&!m.writes);++cases;
    m=model();for(unsigned i=1;i<6;++i)m.reg[(0x104+i*32)/4]=0x100;assert(!claim(&m,&d)&&!m.writes);++cases;
    m=model();m.reg[0x14/4]=32;m.channel=4;assert(claim(&m,&d)&&d.channel==4);assert(cx2_dma_release(&d));assert(m.reg[0x14/4]==32);++cases;
    m=model();m.reg[0x18/4]=32U<<16;m.channel=4;assert(claim(&m,&d)&&d.channel==4);assert(cx2_dma_release(&d));assert(m.reg[0x18/4]==(32U<<16));++cases;
    m=model();m.reg[0x24/4]=0;m.reg[0x1c/4]=1;assert(!claim(&m,&d)&&!m.writes);++cases;
    m=model();m.reg[0x24/4]=0;assert(claim(&m,&d)&&m.reg[0x24/4]==1);assert(cx2_dma_release(&d)&&m.reg[0x24/4]==0);++cases;
    m=model();for(unsigned i=1;i<6;++i)m.reg[(0x104+i*32)/4]=0x2087;
    assert(claim(&m,&d)&&d.channel==5);assert(cx2_dma_start(&d,0x10002000,0x10001000,2048,-1));
    complete(&m);assert(cx2_dma_poll(&d)==CX2_DMA_DONE && cx2_dma_release(&d));assert(m.reg[0x1a4/4]==0x2087);++cases;
    m=model();uint32_t prior[6]={0x01201206,0x2087,0x12300000,0x12400000,0x12500000,0x400};
    for(unsigned i=0;i<6;++i)m.reg[(0x1a0+4*i)/4]=prior[i];
    assert(claim(&m,&d)&&d.channel==5);assert(cx2_dma_start(&d,0x10002000,0x10001000,2048,-1));
    complete(&m);assert(cx2_dma_poll(&d)==CX2_DMA_DONE && cx2_dma_release(&d));
    for(unsigned i=0;i<6;++i){assert(m.reg[(0x1a0+4*i)/4]==prior[i]);}
    ++cases;
    m=model();assert(claim(&m,&d));unsigned n=m.writes;
    assert(!cx2_dma_start(&d,CX2_NAND_FIFO,0x10001001,2048,7));
    assert(!cx2_dma_start(&d,CX2_NAND_FIFO,0x14000000,2048,7));
    assert(!cx2_dma_start(&d,CX2_NAND_FIFO,0x10001000,4097,7));
    assert(!cx2_dma_start(&d,CX2_NAND_FIFO,0x10001000,0,7));
    assert(!cx2_dma_start(&d,CX2_NAND_FIFO,0x10001000,2047,7));
    assert(!cx2_dma_start(&d,CX2_NAND_FIFO,0x10001000,2048,16));
    assert(!cx2_dma_start(&d,CX2_NAND_FIFO,0x10001000,2048,-2));
    assert(!cx2_dma_start(&d,0xb800000c,0x10001000,2048,7));
    assert(!cx2_dma_start(&d,0x10001000,0x10001020,2048,-1));
    assert(!cx2_dma_start(&d,CX2_NAND_FIFO,0x10001000,2048,-1));
    assert(n==m.writes);assert(cx2_dma_release(&d));cases+=10;
    for(int request=-1;request<16;++request){
        m=model();m.reg[0x100/4]=0x12340000;assert(claim(&m,&d));
        assert(cx2_dma_start(&d,request<0?0x10002000:CX2_NAND_FIFO,0x10001000,2048,request));
        assert(m.reg[(0x1a0+20)/4]==512 && !m.reg[(0x1a0+16)/4]);
        assert((m.reg[0x1a0/4]&0x3800)==0x1000 && (m.reg[0x1a0/4]&0x700)==0x200);
        assert((m.reg[0x1a0/4]&6)==6);
        if(request>=0){assert((m.reg[0x1a0/4]&0xe0)==0xc0);assert((m.reg[0x1a4/4]&0xf8)==(0x80U|((unsigned)request<<3)));}
        assert(cx2_dma_poll(&d)==CX2_DMA_PENDING && !cx2_dma_release(&d));
        complete(&m);assert(cx2_dma_poll(&d)==CX2_DMA_DONE && cx2_dma_release(&d));
        for(unsigned i=0;i<6;++i)assert(!m.reg[(0x1a0+4*i)/4]);
        assert(m.reg[0x100/4]==0x12340000);++cases;
    }
    m=model();m.keep_enabled=true;assert(claim(&m,&d));assert(cx2_dma_start(&d,0x10002000,0x10001000,2048,-1));complete(&m);assert(cx2_dma_poll(&d)==CX2_DMA_DONE && cx2_dma_release(&d));++cases;
    m=model();assert(claim(&m,&d));assert(cx2_dma_start(&d,CX2_NAND_FIFO,0x10001000,2048,9));m.reg[0x18/4]|=d.bit;
    assert(cx2_dma_poll(&d)==CX2_DMA_ERROR);cx2_dma_abort(&d);assert(cx2_dma_idle(&d));assert(cx2_dma_release(&d));++cases;
    m=model();m.abort_stuck=true;assert(claim(&m,&d));assert(cx2_dma_start(&d,CX2_NAND_FIFO,0x10001000,2048,3));cx2_dma_abort(&d);n=m.writes;
    assert(!cx2_dma_idle(&d)&&!cx2_dma_release(&d)&&m.writes==n);++cases;
    m=model();assert(claim(&m,&d));assert(cx2_dma_start(&d,CX2_NAND_FIFO,0x10001000,2048,3));m.reg[0x14/4]|=d.bit;
    assert(cx2_dma_poll(&d)==CX2_DMA_PENDING);cx2_dma_abort(&d);assert(cx2_dma_release(&d));++cases;
    m=model();assert(claim(&m,&d));n=m.writes;
    assert(!cx2_dma_start(&d,0x11000000,0xa8000000,153600,-1));assert(m.writes==n);
    printf("PASS %u DMA ownership, setup, completion, abort and isolation cases\n",cases);return 0;
}
