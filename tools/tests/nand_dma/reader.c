#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "spi_nand_reader.h"
typedef struct {
    uint32_t regs[0x104/4],now,command_at,nand_at,row,received,busy_ticks,fifo_depth,burst_left,requested,column;
    unsigned opcode,commands,ops,fifo_reads,reset_at,stall_opcode,ecc;
    uint32_t id,feature_a,feature_b;
    bool active,nand_started,reset_stuck,resetting;
    bool dma_owned,dma_running,dma_disabled,dma_refuse,dma_fail,dma_stall,dma_stop_stuck;
    uint8_t *dma_destination;
    uint32_t dma_bytes,dma_starts,dma_completes,dma_aborts,resets,cache_commands;

} Bus;
static bool cache_command(Bus *b){return b->opcode==0xb||b->opcode==0x3b||b->opcode==0x6b;}
static uint8_t data_byte(Bus *b,unsigned i){return (uint8_t)((i+b->column)*37U+b->row*13U);}
static uint32_t read32(void *ctx,uint32_t reg){
    Bus *b=ctx;++b->ops;
    assert(reg==0x10 || reg==0x18 || reg==0x20 || reg==0x24 || reg==0x28 || reg==0x54 || reg==0x100);
    bool command_ready=(uint32_t)(b->now-b->command_at)>=1U && b->opcode!=b->stall_opcode;
    if(reg==0x10 && b->resetting && !b->reset_stuck && (uint32_t)(b->now-b->reset_at)>=2){
        b->regs[0x10/4]=0;b->active=false;b->resetting=false;b->received=0;b->burst_left=0;
    }
    if(reg==0x24)return b->active && command_ready && ((!cache_command(b)&&b->opcode!=0x9f) || b->received==b->requested);
    if(reg==0x28&&b->regs[0]==0xa0)return b->feature_a;
    if(reg==0x28&&b->regs[0]==0xb0)return b->feature_b;
    if(reg==0x28)return (b->nand_started && (uint32_t)(b->now-b->nand_at)<b->busy_ticks?1U:0U)|(b->ecc<<4);
    if(reg==0x54)return (b->fifo_depth<<8)|b->fifo_depth;
    if(reg==0x18){
        if(b->opcode==0x9f&&b->active&&command_ready&&b->received<4)return 3;
        if(b->active && cache_command(b) && command_ready && b->received<b->requested && !b->burst_left){
            b->burst_left=b->requested-b->received;
            if(b->burst_left>b->fifo_depth)b->burst_left=b->fifo_depth;
        }
        /* Deassert readiness partway through a burst. Unread bytes remain. */
        return 1U | (b->burst_left && b->received%b->fifo_depth==0?2U:0U);
    }
    if(reg==0x100){
        if(b->opcode==0x9f){assert(b->active&&command_ready&&!b->received);b->received=4;return b->id;}
        assert(!b->dma_owned);
        assert(cache_command(b) && b->active && command_ready && b->received<b->requested && b->burst_left);
        unsigned count=b->requested-b->received;if(count>4)count=4;
        assert(b->burst_left>=count);b->burst_left-=count;
        uint32_t result=0;for(unsigned i=0;i<count;++i)result|=(uint32_t)data_byte(b,b->received++)<<(8*i);
        b->fifo_reads+=4;return result;
    }
    return b->regs[reg/4];
}
static uint8_t read8(void *ctx,uint32_t reg){
    Bus *b=ctx;++b->ops;assert(!"byte FIFO reads are unsafe on this controller");assert(reg==0x100 && cache_command(b) && b->active && b->received<b->requested && b->burst_left);
    --b->burst_left;
    ++b->fifo_reads;return data_byte(b,b->received++);
}
static void write32(void *ctx,uint32_t reg,uint32_t value){
    Bus *b=ctx;++b->ops;
    assert(reg==0 || reg==4 || reg==8 || reg==12 || reg==0x10 || reg==0x20 || reg==0x24);
    if(reg==0x24){if(value&1)b->active=false;return;}
    b->regs[reg/4]=value;
    if(reg==0x10 && (value&0x100)){
        assert(!b->dma_owned);++b->resets;
        b->resetting=true;b->reset_at=b->now;
    }
    if(reg!=12)return;
    assert(!b->active);b->active=true;b->command_at=b->now;b->opcode=value>>24;++b->commands;
    if(b->opcode==0xf){
        assert(value==0x0f00010c && (b->regs[0]==0xc0||b->regs[0]==0xa0||b->regs[0]==0xb0) && b->regs[1]==0x01000001 && !b->regs[2]);
    }else if(b->opcode==0x9f){
        assert(value==0x9f000100&&b->regs[1]==0x01080000&&b->regs[2]==4);b->received=0;b->requested=4;
    }else if(b->opcode==0x13){
        assert(value==0x13000102 && b->regs[1]==0x01000003 && !b->regs[2]);
        b->row=b->regs[0];b->nand_at=b->now;b->nand_started=true;
    }else{
        assert(cache_command(b) && (value==0x0b000100||value==0x3b000120||value==0x6b000140) && b->regs[1]==0x01080002);
        assert(!(b->regs[0x20/4]&1U));++b->cache_commands;
        b->column=b->regs[0]&4095U;b->requested=b->regs[2];
        assert((b->regs[0]&4096U)==((b->row/64)&1U)*4096U && b->requested<=2112U-b->column);
        assert((uint32_t)(b->now-b->nand_at)>=b->busy_ticks && b->ecc<2);b->received=0;
    }
}
static void init(SpiNandReader *r,Bus *b){
    memset(b,0,sizeof(*b));b->regs[0x20/4]=0x80;b->regs[0x10/4]=0x11;b->fifo_depth=32;b->id=0x21baef00;b->feature_b=0x18;
    SpiNandBus io={b,read32,read8,write32};assert(spi_nand_init(r,io));
}
static SpiNandStatus step(SpiNandReader *r,Bus *b,uint32_t now,uint32_t budget){
    b->now=now;b->ops=0;b->fifo_reads=0;
    SpiNandStatus result=spi_nand_step(r,now,budget);
    assert(b->ops<=134);assert(b->fifo_reads<=128 && (uint64_t)b->fifo_reads<=(uint64_t)budget+3);
    return result;
}
static void pio_tests(void){
    SpiNandReader r;Bus b;uint8_t guard[2050];
    const unsigned budgets[]={1,3,4,17,128,0xffffffffU};
    const unsigned depths[]={16,32,64,128,12,32};
    for(unsigned j=0;j<6;++j){
        init(&r,&b);memset(guard,0xa5,sizeof(guard));b.busy_ticks=250;b.fifo_depth=depths[j];
        uint32_t start=0xfffffff0U;b.now=start;
        assert(spi_nand_begin(&r,64+j,guard+1,2048,start,10000));
        assert(r.initial_features==(depths[j]<<8|depths[j]) && r.fifo_depth==depths[j]);
        assert(!spi_nand_begin(&r,1,guard+1,2048,start,10000));
        for(unsigned t=0;t<10000 && r.status==SPI_NAND_PENDING;++t)step(&r,&b,start+t,budgets[j]);
        assert(r.status==SPI_NAND_DONE && r.quiescent && r.copied==2048 && r.error==0);
        assert(b.commands>100 && guard[0]==0xa5 && guard[2049]==0xa5 && b.regs[0x20/4]==0x80);
        for(unsigned i=0;i<2048;++i)assert(guard[i+1]==data_byte(&b,i));
        assert(spi_nand_begin(&r,0,guard+1,2048,b.now,10000));spi_nand_cancel(&r);
        assert(step(&r,&b,b.now,128)==SPI_NAND_CANCELED && r.quiescent);
    }
    for(unsigned cancel_at=0;cancel_at<40;++cancel_at){
        init(&r,&b);b.busy_ticks=20;assert(spi_nand_begin(&r,65535,guard+1,2048,0,1000));
        for(unsigned t=0;t<1000 && r.status==SPI_NAND_PENDING;++t){
            if(t==cancel_at)spi_nand_cancel(&r);
            step(&r,&b,t,128);
        }
        assert(r.status==SPI_NAND_CANCELED && r.quiescent && !b.active && b.regs[0x20/4]==0x80);
    }
    for(unsigned ecc=0;ecc<8;++ecc){
        init(&r,&b);b.ecc=ecc;memset(guard,0xa5,sizeof(guard));
        assert(spi_nand_begin(&r,17,guard+1,2048,0,1000));
        for(unsigned t=0;t<1000 && r.status==SPI_NAND_PENDING;++t)step(&r,&b,t,128);
        assert(r.quiescent && (ecc<2?r.status==SPI_NAND_DONE:r.status==SPI_NAND_ERROR));
        if(ecc>=2){assert(r.error==SPI_NAND_ECC && guard[1]==0xa5);}else assert(r.corrected==(ecc==1));
    }
    for(unsigned stall=0;stall<3;++stall){
        init(&r,&b);b.stall_opcode=stall==0?0xf:stall==1?0x13:0xb;
        assert(spi_nand_begin(&r,7,guard+1,2048,0,100));
        for(unsigned t=0;t<300 && r.status==SPI_NAND_PENDING;++t)step(&r,&b,t,128);
        assert(r.status==SPI_NAND_ERROR && r.error==SPI_NAND_TIMEOUT && r.quiescent && b.regs[0x20/4]==0x80);
        assert(b.regs[0x10/4]==0x11);
    }
    init(&r,&b);b.stall_opcode=0xf;b.reset_stuck=true;
    assert(spi_nand_begin(&r,7,guard+1,2048,0,10));
    for(unsigned t=0;t<40 && r.status==SPI_NAND_PENDING;++t)step(&r,&b,t,128);
    assert(r.status==SPI_NAND_ERROR && r.error==SPI_NAND_RESET_FAILED && !r.quiescent);
    assert(!spi_nand_begin(&r,7,guard+1,2048,40,10));
    b.reset_stuck=false;
    step(&r,&b,50,128);
    assert(r.status==SPI_NAND_ERROR && r.error==SPI_NAND_RESET_FAILED && r.quiescent);
    assert(!b.active && b.regs[0x10/4]==0x11 && b.regs[0x20/4]==0x80);
    unsigned ops=b.ops;spi_nand_step(&r,51,128);assert(b.ops==ops);
    init(&r,&b);b.regs[0x10/4]=0x100000;assert(!spi_nand_begin(&r,0,guard,2048,0,100));
    init(&r,&b);b.regs[0x20/4]=2;assert(!spi_nand_begin(&r,0,guard,2048,0,100));
    init(&r,&b);assert(!spi_nand_begin(&r,65536,guard,2048,0,100));
    assert(!spi_nand_begin(&r,0,guard,2047,0,100));assert(!spi_nand_begin(&r,0,guard,2048,0,0));
    assert(!spi_nand_begin(&r,0,&r,2048,0,100));assert(spi_nand_step(NULL,0,128)==SPI_NAND_ERROR);
    uint8_t wide[2114];const unsigned columns[]={0,2052,2111},lengths[]={2112,4,1};
    for(unsigned i=0;i<3;++i){
        init(&r,&b);memset(wide,0xa5,sizeof(wide));
        assert(spi_nand_begin_range(&r,65,columns[i],wide+1,lengths[i],0,10000));
        for(unsigned t=0;t<10000 && r.status==SPI_NAND_PENDING;++t)step(&r,&b,t,17);
        assert(r.status==SPI_NAND_DONE && r.copied==lengths[i] && wide[0]==0xa5 && wide[lengths[i]+1]==0xa5);
        assert(r.column==columns[i]);
        for(unsigned n=0;n<lengths[i];++n)assert(wide[n+1]==data_byte(&b,n));
    }
    init(&r,&b);assert(!spi_nand_begin_range(&r,0,2112,wide,1,0,100));
    assert(!spi_nand_begin_range(&r,0,2052,wide,61,0,100));
    puts("PASS PIO budgets, word tails, ECC, cancellation and delayed reset cleanup");
}

static int dma_begin(void *context,void *destination,uint32_t bytes)
{
    Bus *b=context;
    if(b->dma_disabled)return -1;
    if(b->dma_refuse)return 0;
    assert(!b->active && !b->dma_owned && !(b->regs[0x20/4]&1));
    b->dma_destination=destination;b->dma_bytes=bytes;
    b->dma_owned=b->dma_running=true;++b->dma_starts;return true;
}
static int dma_poll(void *context)
{
    Bus *b=context;
    assert(b->dma_owned && b->active && cache_command(b) && (b->regs[0x20/4]&3)==3);
    if(b->dma_fail && b->received>=32)return -1;
    if(b->dma_stall)return 0;
    if(b->now-b->command_at<1)return 0;
    unsigned n=b->requested-b->received;if(n>32)n=32;
    while(n--){b->dma_destination[b->received]=data_byte(b,b->received);++b->received;}
    if(b->received==b->requested){
        unsigned padded=(b->requested+3U)&~3U;
        for(unsigned i=b->requested;i<padded;++i)b->dma_destination[i]=0xee;
        b->dma_running=false;return 1;
    }
    return 0;
}
static void dma_abort(void *context,bool failed)
{
    Bus *b=context;assert(b->dma_owned);++b->dma_aborts;
    if(failed)b->dma_disabled=true;
    if(!b->dma_stop_stuck)b->dma_running=false;
}
static bool dma_release(void *context,bool completed)
{
    Bus *b=context;assert(b->dma_owned);
    if(b->dma_running)return false;
    if(completed){assert(!b->active);++b->dma_completes;}
    b->dma_owned=false;return true;
}
static void bind(SpiNandReader *r,Bus *b)
{
    assert(spi_nand_set_dma(r,(SpiNandDma){b,dma_begin,dma_poll,dma_abort,dma_release}));
}
static void run(SpiNandReader *r,Bus *b,uint32_t end)
{
    for(unsigned t=b->now+1;t<end && (r->status==SPI_NAND_PENDING || !r->quiescent);++t)
        step(r,b,t,128);
}
static void check_data(Bus *b,const uint8_t *data,unsigned bytes)
{
    for(unsigned i=0;i<bytes;++i)assert(data[i]==data_byte(b,i));
}
int main(void)
{
    pio_tests();
    SpiNandReader r;Bus b;uint8_t output[2176];unsigned cases=0;
    const unsigned spans[][2]={{0,1},{0,2},{0,3},{0,4},{0,5},{0,64},{0,128},
        {0,2048},{0,2049},{0,2112},{3,61},{2047,65},{2048,64},{1,2111}};
    for(unsigned i=0;i<sizeof spans/sizeof *spans;++i){
        unsigned col=spans[i][0],bytes=spans[i][1];
        init(&r,&b);bind(&r,&b);memset(output,0xa5,sizeof output);
        assert(spi_nand_begin_range(&r,65,col,output+32,bytes,0,1000));
        run(&r,&b,3000);
        assert(r.status==SPI_NAND_DONE && r.quiescent && !b.dma_owned && !b.active);
        assert(b.dma_starts==1 && b.dma_completes==1 && !b.dma_aborts);
        assert(r.copied==bytes && b.regs[0x20/4]==0x80);
        check_data(&b,output+32,bytes);
        for(unsigned n=0;n<32;++n)assert(output[n]==0xa5);
        for(unsigned n=32+((bytes+3)&~3U);n<sizeof output;++n)assert(output[n]==0xa5);
        ++cases;
    }
    /* DMA refusal uses the existing CPU path, including a partial final word. */
    init(&r,&b);bind(&r,&b);b.dma_refuse=true;memset(output,0xa5,sizeof output);
    assert(spi_nand_begin_range(&r,8,3,output+32,61,0,1000));run(&r,&b,3000);
    assert(r.status==SPI_NAND_DONE && !b.dma_starts && output[93]==0xa5);
    check_data(&b,output+32,61);++cases;
    init(&r,&b);bind(&r,&b);b.dma_disabled=true;memset(output,0xa5,sizeof output);
    assert(spi_nand_begin(&r,1,output+32,2048,0,1000));run(&r,&b,3000);
    assert(r.status==SPI_NAND_ERROR&&r.error==SPI_NAND_DMA_ERROR&&r.quiescent&&!b.cache_commands);
    for(unsigned i=0;i<sizeof output;++i)assert(output[i]==0xa5);
    ++cases;
    /* Failure/timeout retire DMA and reset SPI, then report the fault without retry. */
    for(unsigned failure=0;failure<2;++failure){
        init(&r,&b);bind(&r,&b);b.dma_fail=!failure;b.dma_stall=failure;
        assert(spi_nand_begin_range(&r,65,1,output+32,2111,0,100));run(&r,&b,1000);
        assert(r.status==SPI_NAND_ERROR && r.quiescent && r.error!=SPI_NAND_OK);
        assert(b.dma_disabled && b.dma_starts==1 && b.dma_aborts==1 && b.resets==1 && b.cache_commands==1);
        ++cases;
    }
    for(unsigned profile=0;profile<5;++profile){
        init(&r,&b);bind(&r,&b);assert(spi_nand_enable_wide(&r));
        if(profile==1)b.id=0x0021baef;
        if(profile==2)b.feature_a=2;
        if(profile==3)b.id=0x12345678;
        if(profile==4)b.feature_b=0x58;
        assert(spi_nand_begin(&r,1,output+32,2048,0,1000));run(&r,&b,3000);
        assert(r.status==SPI_NAND_DONE&&r.profile_ready);
        assert(r.wide_mode==(profile<2?2:profile==2?1:0));check_data(&b,output+32,2048);
        assert(r.quad_commands==(profile<2)&&r.dual_commands==(profile==2));++cases;
    }
    /* Cancel before/during/after command setup and at every DMA data quantum. */
    for(unsigned cancel=0;cancel<90;++cancel){
        init(&r,&b);bind(&r,&b);
        assert(spi_nand_begin(&r,2,output+32,2048,0,1000));
        for(unsigned t=0;t<3000 && r.status==SPI_NAND_PENDING;++t){
            if(t==cancel)spi_nand_cancel(&r);
            step(&r,&b,t,128);
        }
        assert((r.status==SPI_NAND_CANCELED || r.status==SPI_NAND_DONE) && r.quiescent);
        assert(!b.active && !b.dma_owned && !b.dma_disabled);
        assert(spi_nand_begin(&r,3,output+32,2048,b.now,1000));run(&r,&b,b.now+3000);
        assert(r.status==SPI_NAND_DONE);check_data(&b,output+32,2048);++cases;
    }
    /* Failed abort must retain ownership; late cleanup may finally release it. */
    init(&r,&b);bind(&r,&b);b.dma_stall=b.dma_stop_stuck=true;
    assert(spi_nand_begin(&r,1,output+32,2048,0,40));run(&r,&b,150);
    assert(r.status==SPI_NAND_ERROR && r.error==SPI_NAND_DMA_STOP_FAILED && !r.quiescent);
    assert(b.dma_owned && !b.resets && !spi_nand_begin(&r,2,output+32,2048,151,40));
    b.dma_running=false;b.dma_stop_stuck=false;run(&r,&b,300);
    assert(r.status==SPI_NAND_ERROR && r.quiescent && !b.dma_owned && b.resets==1);++cases;
    init(&r,&b);bind(&r,&b);b.dma_stall=b.reset_stuck=true;
    assert(spi_nand_begin(&r,1,output+32,2048,0,40));run(&r,&b,150);
    assert(r.status==SPI_NAND_ERROR && r.error==SPI_NAND_RESET_FAILED && !r.quiescent && !b.dma_owned);
    b.reset_stuck=false;run(&r,&b,300);assert(r.quiescent && r.status==SPI_NAND_ERROR);++cases;
    init(&r,&b);b.regs[0x20/4]=1;assert(!spi_nand_begin(&r,0,output+32,2048,0,100));++cases;
    printf("PASS %u SPI DMA success, refusal, recovery, cancellation and ownership cases\n",cases);
    return 0;
}
