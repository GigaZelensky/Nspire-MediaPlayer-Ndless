/* Production cold-start validation and restart dispatch, using the captured
 * native control-19 state (driver stopped while hardware RUN stays set). */
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

typedef unsigned PortableView;
typedef struct {uint32_t bus,handle,hub_state,hub,hcd;} UsbHostTree;
typedef struct {uint32_t command,port,otg,role_mask;} UsbRegisters;
enum {UI_SEM_TAKE,UI_SEM_GIVE,UI_HCD_CONTROL,UI_HCD_CORE_CONTROL,UI_EHCI_START,UI_IRQ_REGISTER};
static struct {bool classic_cx;uint32_t bsd_locks,bsd_mode;uintptr_t function[6];} api;
static unsigned mapping,mask,borrow_lock_stage,starts,stops,enumerations,reprobes;
static bool resolved,tree_valid,code_valid;
static bool schedule_changed_before_start;
static int start_error;
static unsigned start_count;
static int start_result,start_cleanup_result;
static uint32_t start_phy_before[10],start_phy_after[10];
static const char *stage;
static uint8_t *arena;
static uint32_t base;
#define WORD(at) (*(uint32_t *)(uintptr_t)(at))
#define BUS base
#define HANDLE (base+0x800)
#define HUB_STATE (base+0x1000)
#define HEAD (base+0x1100)
#define DMA (base+0x1200)
#define IRQ (base+0x1300)
#define RESOURCE (base+0x1400)
#define MMIO (base+0x1500)
#define LOCKS (base+0x1600)
#define USB_LOCK (base+0x1700)
#define TREE_LOCK (base+0x1800)
#define MODE (base+0x1901)
#define HUB (base+0x1a00)
#define ADVANCE_EVENT (base+0x1e00)
#define PERIODIC (base+0x2000)
#define CORE 0x81000000U
#define START 0x82000000U
#define REGISTER_IRQ 0x83000000U
static bool memory(uint32_t address,unsigned bytes)
{return bytes<=0x8000 && address>=base && address-base<=0x8000-bytes;}
static unsigned native_critical_enter(void){unsigned old=mask;mask=0xc0;return old;}
static void native_critical_leave(unsigned old){mask=old;}
static bool native_usb_ui_active(void){return true;}
static bool portable_reader_platform_refresh(unsigned *p){assert(p==&mapping);return true;}
static const PortableView *portable_reader_platform_view(unsigned *p){assert(p==&mapping);return &mapping;}
static bool native_usb_ui_role_matches(const PortableView *v,uint32_t address,unsigned role)
{
    assert(v==&mapping);
    return code_valid && ((address==CORE && role==UI_HCD_CORE_CONTROL) ||
        (address==START && role==UI_EHCI_START) || (address==REGISTER_IRQ && role==UI_IRQ_REGISTER));
}
static uint32_t code_branch(uint32_t address)
{
    uint32_t control=(uint32_t)api.function[UI_HCD_CONTROL];
    if(address==control+40 || address==control+188)return CORE;
    if(address==control+124)return REGISTER_IRQ;
    if(address==CORE+248)return START;
    return 0;
}
static bool resume_tree(UsbHostTree *tree,uint32_t *resume)
{
    *tree=(UsbHostTree){BUS,HANDLE,HUB_STATE,HUB,base+0x1c00};*resume=1;
    return tree_valid && WORD(BUS+1772)<=1 && WORD(BUS+1760)<=1 &&
        WORD(BUS+1760)==WORD(HANDLE+32);
}
static bool host_tree(UsbHostTree *tree){uint32_t resume;return resume_tree(tree,&resume);}
static bool usb_registers_for_asic(unsigned asic,UsbRegisters *r)
{(void)asic;*r=(UsbRegisters){MMIO,MMIO+0x20,MMIO+0x24,1U<<21};return true;}
static bool usb_registers_valid(const UsbRegisters *r){(void)r;return true;}
static int take(uint32_t lock,unsigned timeout)
{
    assert(mask==0xc0 && !timeout && (lock==USB_LOCK || lock==TREE_LOCK));
    if(!WORD(lock+24))return -51;
    --WORD(lock+24);return 0;
}
static int give(uint32_t lock)
{assert(mask==0xc0 && !WORD(lock+24));++WORD(lock+24);return 0;}
static int control(uint32_t hcd,unsigned command,unsigned argument)
{
    assert(!mask && hcd==base+0x1c00 && !argument && !WORD(USB_LOCK+24) && !WORD(TREE_LOCK+24));
    if(command==18) {
        ++starts;WORD(BUS+604)=base+0x1d00;
        if(start_error)return start_error;
        WORD(MMIO)=0x80031;WORD(MMIO+4)=0xc000;
        WORD(BUS+1772)=WORD(HUB_STATE+36)=1;
    } else {
        assert(command==19);++stops;
        WORD(BUS+604)=WORD(BUS+1772)=WORD(HUB_STATE+36)=0;
        /* Native stop intentionally leaves CMD/list registers intact. */
    }
    return 0;
}
static int enumerate_empty_root(const UsbHostTree *t,const UsbRegisters *r)
{assert(t->bus==BUS && r->command==MMIO && WORD(BUS+1772));++enumerations;return 0;}
static void reprobe_storage(const UsbHostTree *t){assert(t->bus==BUS);++reprobes;}
static bool retain_bus_lease(uint32_t first,uint32_t second)
{assert(first==USB_LOCK && second==TREE_LOCK);return false;}
static void native_usb_ui_bus_phy(uint32_t out[10])
{
    if(schedule_changed_before_start)WORD(MMIO+4)|=0x8000;
    memset(out,0,10*sizeof(*out));out[0]=1;out[1]=WORD(MMIO);out[2]=WORD(MMIO+4);
}
/* PRODUCTION_NATIVE_RESTART */
static void reset(bool run)
{
    memset(arena,0,0x8000);resolved=tree_valid=code_valid=true;mask=0x80;
    borrow_lock_stage=starts=stops=enumerations=reprobes=start_count=0;
    start_error=start_result=start_cleanup_result=0;
    schedule_changed_before_start=false;
    api.classic_cx=false;api.bsd_locks=LOCKS;api.bsd_mode=MODE;arena[0x1901]=1;
    api.function[UI_SEM_TAKE]=(uintptr_t)take;api.function[UI_SEM_GIVE]=(uintptr_t)give;
    api.function[UI_HCD_CONTROL]=(uintptr_t)control;
    WORD(LOCKS+4)=USB_LOCK;WORD(LOCKS+8)=TREE_LOCK;
    WORD(USB_LOCK+12)=WORD(TREE_LOCK+12)=0x53454d41;
    WORD(USB_LOCK+24)=WORD(TREE_LOCK+24)=1;
    WORD(BUS+600)=IRQ;WORD(IRQ)=2;arena[0x1304]=1;WORD(IRQ+8)=2;
    WORD(BUS+596)=RESOURCE;WORD(RESOURCE)=2;arena[0x1404]=2;
    WORD(BUS+660)=DMA;WORD(BUS+664)=PERIODIC;WORD(BUS+668)=1024;
    WORD(DMA+4)=PERIODIC;WORD(BUS+1740)=HEAD;
    WORD(HEAD+76)=HEAD;WORD(HEAD)=HEAD|2;WORD(HEAD+16)=1;WORD(HEAD+24)=0x40;
    WORD(BUS+52+4)=HUB;
    WORD(BUS+1764)=ADVANCE_EVENT;WORD(ADVANCE_EVENT+12)=0x45564e54;
    WORD(MMIO)=run?0x80031:0;WORD(MMIO+4)=run?0xc000:0x1000;WORD(MMIO+0x20)=0xd;
}
static void unlocked(void)
{assert(WORD(USB_LOCK+24)==1 && WORD(TREE_LOCK+24)==1 && mask==0x80);}
int main(void)
{
#ifdef _WIN32
    arena=VirtualAlloc((void *)(uintptr_t)0x11000000,0x8000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    arena=mmap((void *)(uintptr_t)0x11000000,0x8000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(arena==MAP_FAILED)arena=NULL;
#endif
    assert(arena && (uintptr_t)arena<=UINT32_MAX-0x8000);base=(uint32_t)(uintptr_t)arena;
    for(unsigned running=0;running<2;++running) {
        reset(running);assert(!resume_host(false));
        assert(starts==1 && !stops && enumerations==1 && reprobes==1 && WORD(BUS+604) && WORD(BUS+1772));unlocked();
        assert(start_phy_before[0] && start_phy_before[1]==(running?0x80031U:0U) &&
            start_phy_after[0] && start_phy_after[1]==0x80031U);
    }
    puts("PASS native root-only restart accepts both recorded stopped-driver/RUN and halted-controller states");
    for(unsigned failure=0;failure<12;++failure) {
        reset(true);
        switch(failure) {
        case 0:WORD(BUS+1772)=1;break;
        case 1:WORD(HUB_STATE+36)=1;break;
        case 2:WORD(HEAD)=HEAD+0x82;break;
        case 3:WORD(HEAD+68)=HEAD+0x80;break;
        case 4:WORD(HEAD+24)|=0x80;break;
        case 5:WORD(BUS+52+8)=HUB+0x100;break;
        case 6:WORD(BUS+668)=17;break;
        case 7:code_valid=false;break;
        case 8:WORD(MMIO)|=64;break;
        case 9:WORD(MMIO)|=2;break;
        case 10:WORD(MMIO+4)|=16;break;
        case 11:tree_valid=false;break;
        }
        assert(resume_host(false)<0 && !starts && !stops && !enumerations);unlocked();
    }
    /* Recorded after a clean-reset hub unplug/replug: native control19 has
     * retired all children and IRQ state, but IAAD survives with ASS/PSS=0. */
    reset(true);WORD(MMIO)|=64;WORD(MMIO+4)=8;
    WORD(ADVANCE_EVENT+24)=0x12345678; /* A real pending flag stays untouched. */
    assert(!resume_host(false) && starts==1 && !stops && enumerations==1);
    assert(start_phy_before[1]==0x80071 && start_phy_after[1]==0x80031 &&
        WORD(ADVANCE_EVENT+24)==0x12345678);unlocked();
    for(unsigned event_case=0;event_case<4;++event_case) {
        reset(true);WORD(MMIO)|=64;WORD(MMIO+4)=8;
        if(event_case==0)WORD(BUS+1764)=0;
        if(event_case==1)WORD(ADVANCE_EVENT+12)=0;
        if(event_case==2)WORD(ADVANCE_EVENT+28)=1;
        if(event_case==3)WORD(ADVANCE_EVENT+32)=base+0x1f00;
        assert(resume_host(false)==-28 && !starts && !stops && (WORD(MMIO)&64));unlocked();
    }
    /* That exception must not complete or replace a live owner's request. */
    for(unsigned status=0;status<3;++status) {
        reset(true);WORD(MMIO)|=64;
        WORD(MMIO+4)=status==1?0x4000:status==2?0x8000:0;
        if(!status){WORD(BUS+604)=IRQ;WORD(BUS+1772)=1;}
        assert(resume_host(false)==-28 && !starts && !stops);unlocked();
    }
    puts("PASS stale advance restarts only an authenticated stopped native driver with no linked queue and quiescent schedules");
    reset(true);WORD(MMIO)|=64;WORD(MMIO+4)=8;schedule_changed_before_start=true;
    assert(resume_host(false)==-28 && !starts && !stops && (WORD(MMIO)&64));unlocked();
    reset(true);WORD(BUS+604)=IRQ;WORD(BUS+1772)=1;
    assert(!resume_host(false) && !starts && enumerations==1);unlocked();
    WORD(BUS+1760)=WORD(HANDLE+32)=1;
    assert(resume_host(false)==-29 && !starts);unlocked();
    WORD(MMIO)=0;WORD(MMIO+4)=0x1000; /* Suspended resume still requires saved RUN. */
    assert(resume_host(false)==-29 && !starts);unlocked();
    reset(true);WORD(TREE_LOCK+24)=0;
    assert(resume_host(false)==-51 && !starts && !WORD(TREE_LOCK+24) && WORD(USB_LOCK+24)==1 && mask==0x80);
    reset(true);start_error=5;
    assert(resume_host(false)==5 && starts==1 && stops==1 && !WORD(BUS+604));unlocked();
    puts("PASS restart refuses live children/queues, active or unknown hosts, pending reset/advance, fatal status and contended locks; failed start releases its native IRQ");
    return 0;
}
