/* Actual session claim loop, with deterministic controller/native worker state. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "usb_autoconnect.h"
#include "usb_host_session.h"
static uint32_t regs[64],gate,clock_gate,irq,clock_ms;
static unsigned waits,claims,releases,ready_after,mask,native_ready_after;
static bool native_lease,cancelled,stuck,repair_native,drop_native_on_claim;
static uint32_t native_state[8];
static uint32_t *test_register(uint32_t address)
{
    if(address>=0xb0000000 && address<0xb0000100 && !(address&3))return regs+(address-0xb0000000)/4;
    if(address==0x90140050)return &gate;
    if(address==0x90140060)return &clock_gate;
    if(address==0xdc000010)return &irq;
    assert(!"unexpected register");return &gate;
}
uint32_t nspire_hardware_id(void){return 0x202;}
bool nspire_is_cx2(void){return true;}
static unsigned native_critical_enter(void){unsigned old=mask;mask=0xc0;return old;}
static void native_critical_leave(unsigned value){mask=value;}
static void native_usb_ui_bus_snapshot(uint32_t out[8]){memcpy(out,native_state,sizeof(native_state));}
static void native_usb_ui_bus_details(uint32_t out[16]){memset(out,0,16*sizeof(*out));out[0]=1;out[12]=waits/100;}
static unsigned native_usb_ui_bus_lock_stage(void){return native_lease?3:0;}
static bool native_usb_ui_bus_is_leased(void){return native_lease;}
static int native_usb_ui_bus_acquire(void)
{
    assert(!native_lease && !(regs[4]&(2U|64U)));
    assert(native_state[0]==1 && !native_state[2] && !native_state[3] &&
           native_state[4] && native_state[5]==1);
    if(drop_native_on_claim)native_state[5]=0;
    native_lease=true;++claims;return 0;
}
static void native_usb_ui_bus_release(void){if(native_lease){native_lease=false;++releases;}}
static int native_usb_ui_wait(void)
{
    assert(!native_lease);++waits;clock_ms+=10;
    if(!stuck && waits>=ready_after)regs[4]&=~(2U|64U);
    if(repair_native && waits>=native_ready_after) {
        native_state[0]=1;native_state[2]=native_state[3]=0;
        native_state[4]=0x11000000;native_state[5]=1;
    }
    return 0;
}
static int native_usb_ui_reassert(void){return 0;}
static int native_usb_ui_resume_host_lease(void){return 0;}
static void app_task_io_pause_service(bool pause){(void)pause;}
static uint32_t native_usb_ui_bus_irq_mask(void){return 256;}
static void native_usb_ui_debug(FILE *file){(void)file;}
static uint32_t now(void){return ++clock_ms;}
static bool progress(void *context){(void)context;return !cancelled;}
/* PRODUCTION_HOST_SESSION */
static void reset(uint32_t command,unsigned turns)
{
    memset(regs,0,sizeof(regs));regs[0]=0x01000010;regs[4]=command;
    regs[5]=0xc000;regs[12]=5;regs[32]=2U<<22;
    gate=0xffffffff;clock_gate=0xffebffff;irq=256;mask=0;
    clock_ms=waits=claims=releases=0;ready_after=turns;
    native_lease=leased=cancelled=stuck=repair_native=drop_native_on_claim=false;
    native_ready_after=0;
    memset(native_state,0,sizeof(native_state));
    native_state[0]=1;native_state[4]=0x11000000;native_state[5]=1;native_state[7]=5;
}
int main(void)
{
    char message[160]="";
    reset(0x80031,1);assert(usb_host_session_prepare(now,progress,NULL,message,sizeof(message)));
    assert(waits==1 && claims==1 && native_lease);usb_host_session_release();assert(gate==0xffffffff);
    reset(0x80071,3);assert(usb_host_session_prepare(now,progress,NULL,message,sizeof(message)));
    assert(waits==3 && claims==1 && !(regs[4]&64));
    /* Existing ownership must be released before the native advance worker
     * can retire the pending queue change. No direct doorbell write. */
    regs[4]|=64;waits=0;
    assert(usb_host_session_prepare(now,progress,NULL,message,sizeof(message)));
    assert(waits==3 && claims==2 && releases==1 && native_lease);
    usb_host_session_release();assert(releases==2 && gate==0xffffffff);
    reset(0x80071,1);stuck=true;message[0]=0;
    assert(!usb_host_session_prepare(now,progress,NULL,message,sizeof(message)));
    assert(!native_lease && !leased && !claims && waits<1000 && (regs[4]&64) && gate==0xffffffff);
    reset(0x80033,3);assert(usb_host_session_prepare(now,progress,NULL,message,sizeof(message)));
    assert(waits==3 && !(regs[4]&2));usb_host_session_release();
    reset(0x80071,1);cancelled=true;message[0]=0;
    assert(!usb_host_session_prepare(now,progress,NULL,message,sizeof(message)));
    assert(!claims && !leased && !native_lease && gate==0xffffffff && !strcmp(message,"Cancelled"));
    for(unsigned mode=0;mode<6;++mode) {
        reset(0x80031,1);repair_native=true;native_ready_after=3;
        switch(mode) {
        case 0:native_state[0]=0;break;
        case 1:native_state[4]=0;break;
        case 2:native_state[5]=0;break;
        case 3:native_state[2]=1;break;
        case 4:native_state[3]=1;break;
        case 5:native_state[2]=native_state[3]=1;break;
        }
        assert(usb_host_session_prepare(now,progress,NULL,message,sizeof(message)));
        assert(waits==3 && claims==1 && native_lease);
        usb_host_session_release();
    }
    reset(0x80031,1);native_state[4]=0;message[0]=0;
    assert(!usb_host_session_prepare(now,progress,NULL,message,sizeof(message)));
    assert(!claims && !native_lease && !leased && waits<1000 && gate==0xffffffff);
    reset(0x80031,1);drop_native_on_claim=true;message[0]=0;
    assert(!usb_host_session_prepare(now,progress,NULL,message,sizeof(message)));
    assert(claims==1 && releases==1 && !native_lease && !leased && gate==0xffffffff);
    reset(0x80031,1);native_state[7]=0;
    assert(usb_host_session_prepare(now,progress,NULL,message,sizeof(message)));
    assert(waits==1 && claims==1 && native_lease);
    usb_host_session_release();
    puts("PASS MMIO-ready host waits for authenticated native active/IRQ and consistent resumed state, rechecks after claim, and accepts a coherent empty bus");
    puts("PASS shared host session waits for native doorbell/reset completion, releases an old lease, and bounds cancellation/stuck hardware");
    return 0;
}
