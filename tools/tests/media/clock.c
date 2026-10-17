/* Production read-only clock sampling, with modeled MMIO. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "player/performance_clock.h"
#define CLOCK_PLL 0x90140030U
#define CLOCK_DIVIDER 0x90140020U
#define CLOCK_PENDING 0x90140024U
#define CLOCK_SOURCE 0x90140810U
#define CLOCK_PLL_FIELDS 0xFF1F0311U
#define CLOCK_DIVIDER_FIELDS 0x00F00000U
#define CX_CLOCK_LOAD 0x900B0000U
#define CX_CLOCK_PENDING 0x900B0014U
#define CX_CLOCK_CURRENT 0x900B0024U
#define CX_CLOCK_FIELDS 0xC07FF1FEU
#define EMI_CONTROL 0x90120004U
#define EMI_TIMING74 0x90120074U
#define EMI_TIMING1C 0x9012001CU
static struct {
    bool supported, owned, active;
    uint32_t asic, target_hz, target_pll;
} clock_state;
static uint32_t regs[10];
static unsigned interrupt_mask;
static unsigned native_critical_enter(void)
{unsigned old=interrupt_mask;interrupt_mask=0xc0;return old;}
static void native_critical_leave(unsigned old) {interrupt_mask=old;}
static bool nspire_asic_is_cx(uint32_t asic) {return asic==0x101;}
static uint32_t *reg(uint32_t address)
{
    static const uint32_t addresses[]={CLOCK_PLL,CLOCK_DIVIDER,CLOCK_PENDING,
        CLOCK_SOURCE,CX_CLOCK_LOAD,CX_CLOCK_PENDING,CX_CLOCK_CURRENT,
        EMI_CONTROL,EMI_TIMING74,EMI_TIMING1C};
    assert(interrupt_mask==0xc0);
    for(unsigned i=0;i<10;++i)if(addresses[i]==address)return regs+i;
    assert(false);return NULL;
}
#define CLOCK_WORD(a) (*reg(a))
/* PRODUCTION_CLOCK_SAMPLE */
static PerformanceClockSample sample(void)
{
    uint32_t before[10];memcpy(before,regs,sizeof(regs));
    PerformanceClockSample s;
    interrupt_mask=0x80;
    performance_clock_sample(&s);
    assert(interrupt_mask==0x80 && !memcmp(before,regs,sizeof(regs)));
    return s;
}
int main(void)
{
    clock_state.supported=clock_state.owned=clock_state.active=true;
    clock_state.asic=0x202;clock_state.target_hz=468000000;
    clock_state.target_pll=(39U<<24)|(2U<<16)|0x301;
    regs[0]=clock_state.target_pll;regs[1]=0x10000000;
    regs[3]=0x10;regs[8]=0x55;regs[9]=0x528;
    PerformanceClockSample s=sample();
    assert(s.matches && s.actual_hz==468000000 && s.target_hz==468000000);
    regs[9]=0x502;s=sample();
    assert(!s.matches && s.actual_hz==468000000 && s.timing1c==0x502);
    regs[9]=0x528;regs[0]=(33U<<24)|(2U<<16)|0x301;s=sample();
    assert(!s.matches && s.actual_hz==396000000);
    regs[0]=clock_state.target_pll;regs[2]=1;assert(!sample().matches);
    regs[2]=0;regs[7]=0x400;assert(!sample().matches);regs[7]=0;
    clock_state.supported=false;s=sample();
    assert(!s.matches && !s.actual_hz && !s.pll);
    clock_state.supported=true;clock_state.asic=0x101;
    clock_state.target_pll=(40U<<15)|(1U<<21)|2U|(3U<<12);
    clock_state.target_hz=240000000;regs[4]=regs[6]=clock_state.target_pll;
    s=sample();assert(s.matches && s.actual_hz==240000000 && !s.timing1c);
    puts("PASS read-only clock snapshots detect rate/timing/pending mismatches and preserve IRQ/MMIO on CX/CX II");
    return 0;
}
