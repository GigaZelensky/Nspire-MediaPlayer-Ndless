#include "battery_power.h"
#include <assert.h>
#include <stdio.h>
static uint32_t asic,gate,original,otg,mask,ticks;
static unsigned writes,barriers,cap_reads;
static bool dead,stalled;
uint32_t nspire_hardware_id(void){return asic;}
unsigned native_critical_enter(void){unsigned old=mask;mask=0xc0;return old;}
void native_critical_leave(unsigned old){assert(gate==original);mask=old;}
static uint32_t gate_address(void){return nspire_asic_is_cx(asic)?0x900b0018U:0x90140050U;}
static uint32_t power_read32(uint32_t at)
{
    assert(mask==0xc0);
    if(at==gate_address())return gate;
    if(at==0x900c0004U)return stalled?ticks:--ticks;
    assert(!(gate&(1U<<5)));
    UsbRegisters r;assert(usb_registers_for_asic(asic,&r));
    if(at==r.capability){++cap_reads;return dead?0:r.classic?0x01000040U:0x01000010U;}
    if(r.classic && at==0xb0000000U)return 0xfa05;
    assert(at==r.otg);return otg;
}
static void power_write32(uint32_t at,uint32_t value)
{
    assert(mask==0xc0 && at==gate_address());
    assert(value==original || value==(original&~(1U<<5)));
    ++writes;gate=value;
}
static void power_barrier(void){assert(mask==0xc0);++barriers;}
/* PRODUCTION_SAMPLE */
int main(void)
{
    const uint32_t ids[]={0x101,0x10000101,0x202},masks[]={0,0x40,0x80,0xc0};
    for(unsigned board=0;board<3;++board)for(unsigned irq=0;irq<4;++irq)
    for(unsigned gated=0;gated<2;++gated)for(unsigned state=0;state<4;++state) {
        asic=ids[board];mask=masks[irq];gate=original=0xa5a50000U|(gated?1U<<5:0);
        bool classic=nspire_asic_is_cx(asic),external=false,host=false;
        otg=state==0?(classic?0x0f00U:0x003e0000U):
            state==1?(classic?0x1100U:0x00310000U):
            state==2?(classic?0x0e00U:0x008e0010U):0;
        dead=state==3;stalled=false;ticks=1000;writes=barriers=cap_reads=0;
        bool ok=battery_usb_power(&external,&host);
        assert(ok==(state!=3) && mask==masks[irq] && gate==original);
        assert(writes==gated*2 && barriers==gated*2 && cap_reads<=16);
        if(ok)assert(external==(state==0) && host==(state==2));
    }
    asic=0x202;gate=original=1U<<5;mask=0x80;dead=stalled=true;otg=0;
    writes=barriers=cap_reads=0;bool external,host;
    assert(!battery_usb_power(&external,&host) && cap_reads==65536 && gate==original && mask==0x80);
    asic=0x999;writes=barriers=cap_reads=0;
    assert(!battery_usb_power(&external,&host) && !writes && !cap_reads && mask==0x80);
    puts("PASS live power sampler: clock-gated PC/OTG/removal status, exact gate/IRQ restoration, no controller writes and bounded failed/stalled reads");
}
