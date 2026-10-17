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

enum {PORTABLE_CODE=1,PORTABLE_DATA=2};
typedef struct {void *context;bool (*allow_span)(void *,uint32_t,uint32_t,unsigned);uint32_t code_begin,code_end;} PortableView;
typedef struct {uint32_t bus,handle,hub_state,hub,hcd,hub_owner,otg;} UsbHostTree;
typedef struct {uint32_t command,port,otg,role_mask;} UsbRegisters;
enum {UI_SEM_TAKE,UI_SEM_GIVE,UI_HCD_CONTROL,UI_HCD_CORE_CONTROL,UI_EHCI_START,UI_IRQ_REGISTER,
      UI_HUB_RESUME,UI_HUB_EXPLORE,UI_PORT_RESET,UI_OTG_ATTACH,UI_OTG_ENGINE_CREATE,
      UI_OTG_APP_MODE,UI_OTG_APP_NOTIFY,UI_OTG_SESSION_REQUEST,UI_FUNCTIONS};
static struct {bool classic_cx;uint32_t bsd_locks,bsd_mode;uintptr_t function[UI_FUNCTIONS];} api;
static unsigned mapping,mask,borrow_lock_stage,starts,stops,enumerations,reprobes;
static PortableView view;
static unsigned session_requests,session_helper_calls,tree_gives,usb_gives,lease_attempts;
static int session_result,session_error;
static const char *session_stage;
static uint32_t session_trace[9],denied_code,bad_branch;
static unsigned bad_role;
static uint8_t session_expected[0x8000];
static bool check_session_snapshot;
static bool app_bus_leased,helper_yields_usb;
static bool resolved,tree_valid,code_valid;
static bool schedule_changed_before_request,explore_suspends;
static unsigned port_resets,resumes,enumeration_count;
static int enumeration_result,explore_error;
static uint32_t session_phy_before[10],session_phy_after[10];
static bool retain_allowed;
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
#define HUB_OWNER (base+0x1b00)
#define HUB_INFO (base+0x1b40)
#define ADVANCE_EVENT (base+0x1e00)
#define PERIODIC (base+0x2000)
#define OTG (base+0x1c80)
#define OTG_ATTACH (base+0x4000)
#define ENGINE_CREATE (base+0x4800)
#define APP_MODE (base+0x4900)
#define APP_NOTIFY (base+0x4a00)
#define SESSION_REQUEST (base+0x4b00)
#define ENGINE_TABLE (base+0x4c00)
#define ENGINE (base+0x4d00)
#define POLICY (base+0x4e00)
#define ENGINE_SEM (base+0x4f00)
#define QUEUE_ITEM (base+0x5000)
#define CORE 0x81000000U
#define START 0x82000000U
#define REGISTER_IRQ 0x83000000U
#define HUB_RESUME 0x84000000U
#define EXPLORE 0x85000000U
#define RESET_PORT 0x86000000U
static bool memory(uint32_t address,unsigned bytes)
{return bytes<=0x8000 && address>=base && address-base<=0x8000-bytes;}
static bool allow_span(void *context,uint32_t address,uint32_t bytes,unsigned kind)
{
    assert(context==&mapping && kind==PORTABLE_CODE);
    return memory(address,bytes) && address>=OTG_ATTACH && address<=ENGINE_TABLE &&
        bytes<=ENGINE_TABLE-address && (!denied_code || address!=denied_code);
}
static bool native_usb_ui_bus_is_leased(void){return app_bus_leased;}
static unsigned native_critical_enter(void){unsigned old=mask;mask=0xc0;return old;}
static void native_critical_leave(unsigned old){mask=old;}
static bool native_usb_ui_active(void){return true;}
static bool portable_reader_platform_refresh(unsigned *p){assert(p==&mapping);return true;}
static const PortableView *portable_reader_platform_view(unsigned *p){assert(p==&mapping);return &view;}
static bool native_usb_ui_role_matches(const PortableView *v,uint32_t address,unsigned role)
{
    assert(v==&view);
    return code_valid && role!=bad_role && ((address==CORE && role==UI_HCD_CORE_CONTROL) ||
        (address==START && role==UI_EHCI_START) || (address==REGISTER_IRQ && role==UI_IRQ_REGISTER) ||
        (address==EXPLORE && role==UI_HUB_EXPLORE) || (address==RESET_PORT && role==UI_PORT_RESET) ||
        (address==OTG_ATTACH && role==UI_OTG_ATTACH) ||
        (address==ENGINE_CREATE && role==UI_OTG_ENGINE_CREATE) ||
        (address==APP_MODE && role==UI_OTG_APP_MODE) ||
        (address==APP_NOTIFY && role==UI_OTG_APP_NOTIFY) ||
        (address==SESSION_REQUEST && role==UI_OTG_SESSION_REQUEST));
}
static uint32_t code_branch(uint32_t address)
{
    if(address==bad_branch)return 0;
    uint32_t control=(uint32_t)api.function[UI_HCD_CONTROL];
    if(address==control+40 || address==control+188)return CORE;
    if(address==control+124)return REGISTER_IRQ;
    if(address==CORE+248)return START;
    if(address==HUB_RESUME+36)return EXPLORE;
    if(address==EXPLORE+408)return RESET_PORT;
    if(address==OTG_ATTACH+0x13c)return ENGINE_CREATE;
    if(address==APP_NOTIFY+0x5c)return SESSION_REQUEST;
    return 0;
}
static int resume_existing(uint32_t hcd) {
    assert(!mask && hcd==base+0x1c00); ++resumes;
    WORD(BUS+1760)=WORD(HANDLE+32)=0;
    WORD(MMIO)=0x80031;WORD(MMIO+4)=0xc000;return 0;
}
static bool resume_tree(UsbHostTree *tree,uint32_t *resume)
{
    *tree=(UsbHostTree){BUS,HANDLE,HUB_STATE,HUB,base+0x1c00,HUB_OWNER,OTG};*resume=1;
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
{
    assert(mask==0xc0 && (lock==USB_LOCK || lock==TREE_LOCK) && !WORD(lock+24));
    if(lock==TREE_LOCK)++tree_gives;else ++usb_gives;
    ++WORD(lock+24);return 0;
}
static int request_owner_session(void)
{
    assert(!mask && !WORD(USB_LOCK+24) && WORD(TREE_LOCK+24)==1 && tree_gives==session_helper_calls+1);
    assert(!starts && !stops && !enumerations && !port_resets && !lease_attempts);
    if(check_session_snapshot) {
        unsigned offset=USB_LOCK-base+24;
        assert(!memcmp(arena,session_expected,offset));
        assert(!memcmp(arena+offset+4,session_expected+offset+4,0x8000-offset-4));
    }
    ++session_helper_calls;
    /* The real helper may give/reacquire USB while its native queue drains.
     * The caller must have released TREE before permitting that schedule. */
    if(helper_yields_usb) {
        native_critical_enter();give(USB_LOCK);native_critical_leave(0);
        assert(WORD(TREE_LOCK+24)==1);
        native_critical_enter();assert(!take(USB_LOCK,0));native_critical_leave(0);
    }
    return session_error;
}
static int control(uint32_t hcd,unsigned command,unsigned argument)
{
    (void)hcd;(void)argument;
    if(command==18)++starts;else ++stops;
    assert(!"The app must leave cold HCD control to the native OTG owner");
    return 0;
}
static uint32_t resume_method(uint32_t owner,unsigned role)
{assert(owner==HUB_OWNER && role==UI_HUB_RESUME);return HUB_RESUME;}
static int reset_port(uint32_t hub,unsigned port,void *status)
{assert(!mask && hub==HUB && port==1 && status);++port_resets;WORD(MMIO+0x20)|=4;return 0;}
static int explore_root(uint32_t hub,unsigned discover)
{
    assert(!mask && hub==HUB && discover==1 && !WORD(USB_LOCK+24) && !WORD(TREE_LOCK+24));
    ++enumerations;
    /* Model only the witnessed native change acknowledgement, not attachment. */
    WORD(MMIO+0x20)&=~(2U|8U);
    if(explore_suspends) {
        WORD(BUS+1760)=WORD(HANDLE+32)=1;WORD(BUS+648)=0x80031;
        WORD(MMIO)=0;WORD(MMIO+4)=0x1000;
    }
    return explore_error;
}
static void reprobe_storage(const UsbHostTree *t){assert(t->bus==BUS);++reprobes;}
static bool retain_bus_lease(uint32_t first,uint32_t second)
{
    assert(first==USB_LOCK && second==TREE_LOCK);++lease_attempts;
    if(retain_allowed)app_bus_leased=true;
    return retain_allowed;
}
static void native_usb_ui_bus_phy(uint32_t out[10])
{
    if(schedule_changed_before_request)WORD(MMIO+4)|=0x8000;
    memset(out,0,10*sizeof(*out));out[0]=1;out[1]=WORD(MMIO);out[2]=WORD(MMIO+4);
}
/* PRODUCTION_NATIVE_RESTART */
static void reset(bool run)
{
    memset(arena,0,0x8000);resolved=tree_valid=code_valid=true;mask=0x80;
    borrow_lock_stage=starts=stops=enumerations=reprobes=0;
    port_resets=resumes=enumeration_count=0;enumeration_result=explore_error=0;
    retain_allowed=false;
    session_requests=session_helper_calls=tree_gives=usb_gives=lease_attempts=0;
    session_result=session_error=0;session_stage="not requested";memset(session_trace,0,sizeof(session_trace));
    denied_code=bad_branch=0;bad_role=~0U;check_session_snapshot=false;
    app_bus_leased=helper_yields_usb=false;
    view=(PortableView){&mapping,allow_span,OTG_ATTACH,ENGINE_TABLE};
    schedule_changed_before_request=explore_suspends=false;
    api.classic_cx=false;api.bsd_locks=LOCKS;api.bsd_mode=MODE;arena[0x1901]=1;
    api.function[UI_SEM_TAKE]=(uintptr_t)take;api.function[UI_SEM_GIVE]=(uintptr_t)give;
    api.function[UI_HCD_CONTROL]=(uintptr_t)control;
    api.function[UI_OTG_ATTACH]=OTG_ATTACH;
    WORD(OTG_ATTACH+0x130)=0xe1a00004;WORD(OTG_ATTACH+0x134)=0xe59f1568;
    WORD(OTG_ATTACH+0x138)=0xe284200c;WORD(OTG_ATTACH+0x6a4)=ENGINE_TABLE;
    WORD(OTG+0x0c)=ENGINE;WORD(ENGINE+0x0c)=ENGINE_CREATE;WORD(ENGINE+0x10)=ENGINE_TABLE;WORD(ENGINE+0x14)=OTG;
    WORD(ENGINE+0x28)=APP_MODE;WORD(ENGINE+0x2c)=APP_NOTIFY;
    WORD(SESSION_REQUEST+0x30)=WORD(APP_MODE+0x40)=WORD(ENGINE+0x44)=POLICY;
    WORD(POLICY)=1;WORD(POLICY+4)=ENGINE;
    WORD(ENGINE+0x68)=ENGINE_SEM;WORD(ENGINE_SEM+12)=0x53454d41;WORD(ENGINE_SEM+24)=1;
    WORD(ENGINE+0x70)=ENGINE+0x6c;
    WORD(LOCKS+4)=USB_LOCK;WORD(LOCKS+8)=TREE_LOCK;
    WORD(USB_LOCK+12)=WORD(TREE_LOCK+12)=0x53454d41;
    WORD(USB_LOCK+24)=WORD(TREE_LOCK+24)=1;
    WORD(BUS+600)=IRQ;WORD(IRQ)=2;arena[0x1304]=1;WORD(IRQ+8)=2;
    WORD(BUS+596)=RESOURCE;WORD(RESOURCE)=2;arena[0x1404]=2;
    WORD(BUS+660)=DMA;WORD(BUS+664)=PERIODIC;WORD(BUS+668)=1024;
    WORD(DMA+4)=PERIODIC;WORD(BUS+1740)=HEAD;
    WORD(HEAD+76)=HEAD;WORD(HEAD)=HEAD|2;WORD(HEAD+16)=1;WORD(HEAD+24)=0x40;
    WORD(BUS+52+4)=HUB;
    WORD(HUB+96)=HUB_INFO;WORD(HUB_INFO)=EXPLORE;WORD(HUB_INFO+4)=HUB_STATE;
    arena[0x1b40+10]=1;arena[0x1000+25]=arena[0x1000+32]=1;
    WORD(BUS+1764)=ADVANCE_EVENT;WORD(ADVANCE_EVENT+12)=0x45564e54;
    WORD(MMIO)=run?0x80031:0;WORD(MMIO+4)=run?0xc000:0x1000;WORD(MMIO+0x20)=0xd;WORD(MMIO+0x24)=0x8e0010;
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
        reset(running);if(running)WORD(MMIO+4)=0;
        memcpy(session_expected,arena,sizeof(session_expected));check_session_snapshot=true;
        assert(!resume_host(false));
        assert(!starts && !stops && !enumerations && !reprobes &&
            session_requests==1 && session_helper_calls==1 && !lease_attempts &&
            !WORD(BUS+604) && !WORD(BUS+1772));unlocked();
        assert(session_phy_before[0] && session_phy_before[1]==(running?0x80031U:0U) &&
            session_phy_after[0] && session_phy_after[1]==session_phy_before[1]);
        assert(!memcmp(arena,session_expected,sizeof(session_expected)));
    }
    puts("PASS powered/connected cold roots queue their native owner from recorded RUN or halted states, without app HCD start");
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
    assert(!resume_host(false) && !starts && !stops && !enumerations && session_helper_calls==1);
    assert(session_phy_before[1]==0x80071 && session_phy_after[1]==0x80071 &&
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
    puts("PASS stale advance reaches only an authenticated native owner with no linked queue and quiescent schedules; no flag or command is fabricated");
    reset(true);WORD(MMIO)|=64;WORD(MMIO+4)=8;schedule_changed_before_request=true;
    assert(resume_host(false)==-28 && !starts && !stops && (WORD(MMIO)&64));unlocked();
    reset(true);WORD(BUS+604)=IRQ;WORD(BUS+1772)=WORD(HUB_STATE+36)=1;
    assert(!resume_host(false) && !starts && enumerations==1);unlocked();
    WORD(BUS+1760)=WORD(HANDLE+32)=1;
    assert(resume_host(false)==-29 && !starts);unlocked();
    WORD(MMIO)=0;WORD(MMIO+4)=0x1000; /* Suspended resume still requires saved RUN. */
    assert(resume_host(false)==-29 && !starts);unlocked();
    reset(true);WORD(TREE_LOCK+24)=0;
    assert(resume_host(false)==-51 && !starts && !WORD(TREE_LOCK+24) && WORD(USB_LOCK+24)==1 && mask==0x80);
    puts("PASS owner request refuses live children/queues, active or unknown hosts, pending reset/advance, fatal status and contended locks");
    /* A dropped session is queued through its existing native OTG owner.
     * Native requests own power/reset ordering: the app must not start the
     * HCD, alter MMIO/IRQ/queue state, enumerate, or retain a bus lease. */
    const uint32_t session_otg[]={0x12ea0,0x8e0030,0x8e0010U&~0x80000U,0x8e0010,0x8e0010};
    const uint32_t session_port[]={0,0xd,0xd,0,0xd};
    for(unsigned physical=0;physical<5;++physical)
        for(unsigned retaining=0;retaining<2;++retaining)
            for(unsigned yield=0;yield<2;++yield) {
                reset(true);WORD(MMIO)=0x80b00;WORD(MMIO+4)=0x1000;
                WORD(MMIO+0x24)=session_otg[physical];WORD(MMIO+0x20)=session_port[physical];
                helper_yields_usb=yield!=0;check_session_snapshot=true;
                memcpy(session_expected,arena,sizeof(session_expected));
                assert(!resume_host(retaining!=0));
                assert(session_requests==1 && session_helper_calls==1 && !session_result &&
                    !strcmp(session_stage,"native request accepted"));
                assert(!starts && !stops && !enumerations && !port_resets && !reprobes &&
                    !lease_attempts && !app_bus_leased && tree_gives==1 && usb_gives==1+yield);
                assert(!memcmp(arena,session_expected,sizeof(session_expected)));
                assert(session_trace[0]==ENGINE && session_trace[5]==0 &&
                    session_trace[6]==ENGINE+0x6c && session_trace[7]==POLICY && session_trace[8]==1);
                if(retaining)assert(borrow_lock_stage==2);
                unlocked();
            }
    puts("PASS captured dropped VBUS/disconnected and already powered cold states queue native OTG ownership with TREE released, USB serialized, no app register writes or ready lease");
    for(unsigned error=0;error<3;++error) {
        reset(false);WORD(MMIO+0x24)=0x12ea0;WORD(MMIO+0x20)=0;
        session_error=error==0?5:error==1?7:-51;
        memcpy(session_expected,arena,sizeof(session_expected));check_session_snapshot=true;
        assert(resume_host(true)==session_error && session_result==session_error && session_helper_calls==1 &&
            !strcmp(session_stage,"native request refused"));
        assert(!starts && !stops && !lease_attempts && tree_gives==1 && usb_gives==1);
        assert(!memcmp(arena,session_expected,sizeof(session_expected)));unlocked();
    }
    /* A well-formed already queued item is valid owner state; the helper's
     * own semaphore/queue decides whether to drain or append. */
    reset(false);WORD(MMIO+0x24)=0x12ea0;WORD(MMIO+0x20)=0;
    WORD(ENGINE+0x6c)=QUEUE_ITEM;WORD(ENGINE+0x70)=QUEUE_ITEM+8;
    assert(!resume_host(false) && session_helper_calls==1 && session_trace[5]==QUEUE_ITEM);
    unlocked();
    for(unsigned invalid=0;invalid<56;++invalid) {
        reset(false);WORD(MMIO+0x24)=0x12ea0;WORD(MMIO+0x20)=0;
        switch(invalid) {
        case 0:api.function[UI_OTG_ATTACH]=0;break;
        case 1:WORD(OTG_ATTACH+0x130)^=1;break;
        case 2:WORD(OTG_ATTACH+0x134)^=1;break;
        case 3:WORD(OTG_ATTACH+0x138)^=1;break;
        case 4:bad_branch=OTG_ATTACH+0x13c;break;
        case 5:WORD(OTG_ATTACH+0x6a4)=0;break;
        case 6:WORD(OTG+0xc)=0;break;
        case 7:WORD(ENGINE+0x10)=ENGINE_TABLE+4;break;
        case 8:WORD(ENGINE+0x14)=OTG+4;break;
        case 9:WORD(ENGINE)|=1;break;
        case 10:WORD(ENGINE+0x2c)=APP_NOTIFY+4;break;
        case 11:WORD(ENGINE+0x28)=APP_MODE+4;break;
        case 12:bad_branch=APP_NOTIFY+0x5c;break;
        case 13:WORD(SESSION_REQUEST+0x30)=0;break;
        case 14:WORD(APP_MODE+0x40)=POLICY+4;break;
        case 15:WORD(ENGINE+0x44)=POLICY+4;break;
        case 16:WORD(POLICY+4)=ENGINE+4;break;
        case 17:WORD(POLICY)=2;break;
        case 18:WORD(ENGINE+0x68)=0;break;
        case 19:WORD(ENGINE_SEM+12)=0;break;
        case 20:WORD(ENGINE_SEM+24)=2;break;
        case 21:WORD(ENGINE+0x70)=0;break;
        case 22:WORD(ENGINE+0x6c)=QUEUE_ITEM;WORD(ENGINE+0x70)=QUEUE_ITEM+8;WORD(QUEUE_ITEM+8)=QUEUE_ITEM;break;
        case 23:WORD(ENGINE+0x70)=QUEUE_ITEM+8;break;
        case 24:WORD(ENGINE+0x6c)=1;break;
        case 25:WORD(HUB_INFO+56)=QUEUE_ITEM;break;
        case 26:WORD(HUB_INFO)=0;break;
        case 27:WORD(HUB_INFO+4)=HUB_STATE+4;break;
        case 28:arena[0x1b40+10]=2;break;
        case 29:denied_code=OTG_ATTACH+0x130;break;
        case 30:denied_code=OTG_ATTACH+0x6a4;break;
        case 31:denied_code=SESSION_REQUEST+0x30;break;
        case 32:denied_code=APP_MODE+0x40;break;
        case 33:app_bus_leased=true;break;
        case 34:WORD(MMIO+0x24)|=0x200000;break;
        case 35:WORD(MMIO+0x24)|=0x100000;break;
        case 36:WORD(MMIO+4)|=0x4000;break;
        case 37:WORD(MMIO+4)|=0x8000;break;
        case 38:api.classic_cx=true;break;
        case 39:arena[0x1901]=2;break;
        case 40:WORD(BUS+52+8)=QUEUE_ITEM;break;
        case 41:WORD(HEAD)=HEAD+0x82;break;
        case 42:tree_valid=false;break;
        case 43:WORD(BUS+1772)=1;break;
        case 44:WORD(HUB_STATE+36)=1;break;
        case 45:WORD(HUB+96)=0;break;
        case 46:WORD(HEAD+68)=HEAD+0x80;break;
        case 47:bad_role=UI_OTG_ENGINE_CREATE;break;
        case 48:bad_role=UI_OTG_SESSION_REQUEST;break;
        case 49:WORD(ENGINE+0xc)=0;break;
        case 50:WORD(ENGINE+0xc)=POLICY;break;
        case 51:denied_code=ENGINE_CREATE;break;
        case 52:WORD(ENGINE+0xc)=ENGINE_CREATE+1;break;
        case 53:view.code_begin=ENGINE_CREATE+4;break;
        case 54:view.code_end=ENGINE_CREATE;break;
        case 55:view.code_end=2;break;
        }
        memcpy(session_expected,arena,sizeof(session_expected));
        assert(resume_host(true)<0 && !starts && !stops && !session_requests &&
            !session_helper_calls && !lease_attempts && !enumerations && !port_resets);
        assert(!memcmp(arena,session_expected,sizeof(session_expected)));unlocked();
    }
    reset(false);WORD(MMIO+0x24)=0x12ea0;WORD(MMIO+0x20)=0;
    WORD(POLICY)=0;WORD(ENGINE_SEM+24)=0;
    assert(!resume_host(true) && session_helper_calls==1 && session_trace[8]==0 &&
        !WORD(ENGINE_SEM+24) && !lease_attempts);unlocked();
    /* The same under-construction child guard protects a powered cold root,
     * not merely a dropped-session binding. Do not overwrite live EP0 work. */
    reset(false);WORD(HUB_INFO+56)=QUEUE_ITEM;
    assert(resume_host(false)==-55 && !starts && !session_helper_calls);unlocked();
    puts("PASS native session validates constructor, engine/table, callbacks, policy and queue; owner errors propagate, unknown/live owners retain their state");
    /* Native power can become ready before its timer finishes wait-bcon.
     * Neither another poll nor a freshly opened app may shortcut that owner
     * with a second HCD18. Keep its queue intact until it publishes the host. */
    reset(false);WORD(MMIO+0x24)=0x12ea0;WORD(MMIO+0x20)=0;
    assert(!resume_host(true) && session_helper_calls==1 && !starts && !lease_attempts);unlocked();
    WORD(MMIO+0x24)=0x8e0010;WORD(MMIO+0x20)=3;
    WORD(ENGINE+0x6c)=QUEUE_ITEM;WORD(ENGINE+0x70)=QUEUE_ITEM+8;
    assert(!resume_host(true) && session_helper_calls==2 && !starts && !lease_attempts);unlocked();
    assert(WORD(ENGINE+0x6c)==QUEUE_ITEM && WORD(ENGINE+0x70)==QUEUE_ITEM+8);
    /* Emulate new application statics without erasing native engine state. */
    session_requests=session_helper_calls=tree_gives=usb_gives=0;
    session_result=0;memset(session_trace,0,sizeof(session_trace));
    assert(!resume_host(true) && session_requests==1 && session_helper_calls==1 &&
        !starts && !stops && !lease_attempts && !WORD(BUS+604) && !WORD(BUS+1772));unlocked();
    /* The owner, outside the app function, completes reset/start/publication. */
    WORD(MMIO)=0x80031;WORD(MMIO+4)=0xc000;WORD(MMIO+0x20)=0xd;
    WORD(BUS+604)=IRQ;WORD(BUS+1772)=WORD(HUB_STATE+36)=1;
    WORD(ENGINE+0x6c)=0;WORD(ENGINE+0x70)=ENGINE+0x6c;
    retain_allowed=true;
    assert(!resume_host(true) && !starts && !stops && session_helper_calls==1 &&
        enumerations==1 && reprobes==1 && lease_attempts==1 && app_bus_leased &&
        !WORD(USB_LOCK+24) && !WORD(TREE_LOCK+24) && mask==0x80);
    native_critical_enter();give(TREE_LOCK);give(USB_LOCK);native_critical_leave(0x80);
    app_bus_leased=false;unlocked();
    puts("PASS repeated polls and application reopen defer a power-ready cold owner; only fresh native publication permits the app bus lease");
    /* Already-running root changes must still dispatch explore(1), never
     * reset a disconnected port or treat the owner-queued cold path as ready. */
    {
        reset(true);WORD(MMIO+4)=0;WORD(MMIO+0x20)=0x80a;
        WORD(BUS+604)=IRQ;WORD(BUS+1772)=WORD(HUB_STATE+36)=1;
        assert(!resume_host(false) && enumerations==1 && !port_resets && !reprobes);
        assert(!(WORD(MMIO+0x20)&(1U|2U|8U)) && !starts);unlocked();
        assert(!resume_host(false) && enumerations==1 && !port_resets);unlocked();
        WORD(MMIO+0x20)=3; /* A later real insertion needs its ordinary reset. */
        assert(!resume_host(false) && enumerations==2 && port_resets==1 && reprobes==1);unlocked();
    }
    reset(true);WORD(BUS+604)=IRQ;WORD(BUS+1772)=WORD(HUB_STATE+36)=1;
    WORD(MMIO+0x20)=0x80a;WORD(BUS+52+8)=HUB+0x100;
    assert(!resume_host(false) && !enumerations && !port_resets && !reprobes);unlocked();
    reset(true);WORD(BUS+604)=IRQ;WORD(BUS+1772)=WORD(HUB_STATE+36)=1;
    WORD(MMIO+0x20)=0x80a;WORD(HUB_INFO+56)=HUB+0x100;
    assert(resume_host(false)==-42 && !enumerations && !port_resets);unlocked();
    reset(true);WORD(BUS+604)=IRQ;WORD(BUS+1772)=WORD(HUB_STATE+36)=1;
    WORD(MMIO+0x20)=0x80a;explore_error=5;
    assert(resume_host(false)==5 && enumerations==1 && !port_resets && !reprobes);unlocked();
    reset(true);WORD(BUS+604)=IRQ;WORD(BUS+1772)=WORD(HUB_STATE+36)=1;
    WORD(BUS+1760)=WORD(HANDLE+32)=1;WORD(BUS+648)=0x80031;
    WORD(MMIO)=0;WORD(MMIO+4)=0x1000;WORD(MMIO+0x20)=0;
    assert(!resume_host(false) && resumes==1 && !enumerations && !port_resets && !reprobes);unlocked();
    reset(true);WORD(BUS+604)=IRQ;WORD(BUS+1772)=WORD(HUB_STATE+36)=1;
    WORD(MMIO+0x20)=0x80a;explore_suspends=true;
    assert(!resume_host(false) && enumerations==1 && WORD(HANDLE+32) && !WORD(MMIO));unlocked();
    assert(!resume_host(false) && resumes==1 && enumerations==1 && !WORD(HANDLE+32));unlocked();
    puts("PASS disconnected root changes reach native explore(1), empty ports are not reset, later insertion can enumerate, and authentic suspended hosts wake without CONNECT");
    return 0;
}
