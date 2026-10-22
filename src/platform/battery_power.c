#include "battery_power.h"
#if defined(__arm__)
#include "../player/native_interrupts.h"
static uint32_t power_read32(uint32_t at){return *(const volatile uint32_t *)(uintptr_t)at;}
static void power_write32(uint32_t at,uint32_t value){*(volatile uint32_t *)(uintptr_t)at=value;}
static void power_barrier(void){__asm__ volatile("mcr p15,0,%0,c7,c10,4"::"r"(0):"memory");}
static void battery_power_sample(const UsbRegisters *r,uint32_t *cap,uint32_t *identity,uint32_t *otg)
{
    /* Same bounded, gate-preserving read used by USB discovery. After cable
     * removal a gated register window returns zeros, not cable status. */
    uint32_t gate=r->classic?0x900b0018U:0x90140050U,saved=power_read32(gate);
    if(saved&(1U<<5)){power_write32(gate,saved&~(1U<<5));power_barrier();}
    uint32_t started=power_read32(0x900c0004U);unsigned polls=0;
    do {
        *cap=power_read32(r->capability);++polls;
    } while((*cap&0xffff00ffU)!=(r->classic?0x01000040U:0x01000010U) &&
            started-power_read32(0x900c0004U)<16U && polls<65536U);
    *identity=r->classic?power_read32(0xb0000000U):*cap;
    *otg=power_read32(r->otg);
    if(saved&(1U<<5)){power_write32(gate,saved);power_barrier();}
}
bool battery_usb_power(bool *external,bool *host)
{
    uint32_t asic=nspire_hardware_id();UsbRegisters r;
    if(!external || !host || !usb_registers_for_asic(asic,&r))return false;
    unsigned mask=native_critical_enter();
    uint32_t cap,identity,otg;battery_power_sample(&r,&cap,&identity,&otg);
    bool ok=battery_power_decode(asic,cap,identity,otg,external,host);
    native_critical_leave(mask);return ok;
}
#else
bool battery_usb_power(bool *external,bool *host){(void)external;(void)host;return false;}
#endif
