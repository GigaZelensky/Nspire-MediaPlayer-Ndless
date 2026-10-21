/* Actual bounded native queue observer; no controller/device is emulated. */
#include "usb_phase_timing.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BASE 0x1000U
#define HEAD 0x2000U
#define LIVE0 0x2080U
#define LIVE1 0x2100U
#define FREE0 0x2200U
#define FREE1 0x2280U
#define LOCKS 0x1800U
#define USB_SEM 0x1880U
#define TREE_SEM 0x1900U
#define QTD 0x3000U
#define EVENT 0x3400U
static uint32_t ram[0x4000/4],detail[16],native[8],phy[10];
static unsigned mask,captures,refreshes,phy_reads;
static bool recording,tree_valid;
static struct {bool classic_cx;uint32_t bsd_locks;} api;
typedef struct {uint32_t bus;} UsbHostTree;
static unsigned mapping;
static bool resolved;
static UsbQueueTimingState captured;
static UsbQueueTimingPrivate private_copy;
static uint32_t *word(uint32_t address)
{assert(!(address&3U) && address>=BASE && address<BASE+sizeof(ram));return ram+(address-BASE)/4;}
#define WORD(address) (*word(address))
static bool memory(uint32_t address,unsigned bytes)
{return !(address&3U) && bytes<=sizeof(ram) && address>=BASE && address-BASE<=sizeof(ram)-bytes;}
static unsigned native_critical_enter(void){unsigned old=mask;mask=0xc0;return old;}
static void native_critical_leave(unsigned old){mask=old;}
static bool portable_reader_platform_refresh(unsigned *p)
{assert(p==&mapping && mask==0xc0);++refreshes;return true;}
static bool host_tree(UsbHostTree *tree){tree->bus=BASE;return tree_valid;}
static void native_usb_ui_bus_details(uint32_t out[16]){assert(mask==0xc0);memcpy(out,detail,sizeof(detail));}
static void native_usb_ui_bus_snapshot(uint32_t out[8]){assert(mask==0xc0);memcpy(out,native,sizeof(native));}
static void native_usb_ui_bus_phy(uint32_t out[10]){assert(mask==0xc0);++phy_reads;memcpy(out,phy,sizeof(phy));}
bool usb_phase_timing_enabled(void){return recording;}
void usb_phase_timing_queue_state(const char *label,const UsbQueueTimingState *state,
                                 const UsbQueueTimingPrivate *private_state)
{
    assert(!strcmp(label,"test_boundary") && mask==0xc0);
    captured=*state;private_copy=private_state?*private_state:(UsbQueueTimingPrivate){0};++captures;
}
/* PRODUCTION_QUEUE_CAPTURE */
static void reset(void)
{
    memset(ram,0,sizeof(ram));memset(detail,0,sizeof(detail));memset(native,0,sizeof(native));
    memset(phy,0,sizeof(phy));memset(&captured,0,sizeof(captured));
    recording=resolved=tree_valid=true;api.classic_cx=false;api.bsd_locks=LOCKS;
    mask=0x80;captures=refreshes=phy_reads=0;
    WORD(LOCKS+4)=USB_SEM;WORD(LOCKS+8)=TREE_SEM;
    WORD(USB_SEM+12)=WORD(TREE_SEM+12)=0x53454d41;
    WORD(USB_SEM+24)=WORD(TREE_SEM+24)=1;
    const uint32_t nodes[]={HEAD,LIVE0,LIVE1,FREE0,FREE1};
    for(unsigned i=0;i<5;++i){WORD(nodes[i]+76)=nodes[i];WORD(nodes[i]+16)=1;}
    WORD(HEAD+68)=LIVE0;WORD(LIVE0+68)=LIVE1;WORD(FREE0+68)=FREE1;
    WORD(LIVE0+4)=0x80406004;WORD(LIVE0+16)=QTD;WORD(QTD+8)=0x001f8c80;
    WORD(LIVE1+4)=0x80406004;WORD(LIVE1+16)=QTD;WORD(LIVE1+24)=0x40;
    WORD(FREE0+16)=QTD;WORD(HEAD+24)=0x40;
    detail[0]=1;detail[1]=2;detail[2]=0x3f;detail[5]=EVENT;
    detail[6]=HEAD;detail[7]=HEAD;detail[9]=0x40;detail[11]=LIVE0;detail[12]=FREE0;detail[14]=LIVE1;
    native[0]=1;native[4]=0x1700;native[5]=1;
    phy[0]=1;phy[1]=0x80071;phy[2]=0xc000;phy[3]=0x80d;phy[4]=0x8e0010;
    WORD(EVENT+12)=0x45564e54;
}
static void observe(void)
{
    uint32_t before[sizeof(ram)/sizeof(*ram)],before_phy[10];
    memcpy(before,ram,sizeof(ram));memcpy(before_phy,phy,sizeof(phy));
    UsbQueueTimingPrivate private_state={.token=0x001f8c80,.saved_list=FREE0,.native_result=12};
    native_usb_ui_queue_capture("test_boundary",&private_state);
    assert(mask==0x80 && !memcmp(before,ram,sizeof(ram)) && !memcmp(before_phy,phy,sizeof(phy)));
}
int main(void)
{
    reset();recording=false;observe();assert(!captures && !refreshes && !phy_reads);
    reset();observe();
    assert(captures==1 && captured.flags==(USB_QUEUE_VALID|USB_QUEUE_MMIO|USB_QUEUE_ANCHOR_LIVE|
        USB_QUEUE_FIRST_QTD_VALID|USB_QUEUE_ANCHOR_QTD_VALID));
    assert(captured.live_nodes==3 && captured.free_nodes==2 && captured.first_qh==LIVE0 &&
        captured.anchor==LIVE1 && captured.first_qtd_token==0x001f8c80 && captured.anchor_qtd_token==0x001f8c80 &&
        captured.command==0x80071 && captured.event_waiters==0 && captured.usb_count==1 &&
        captured.tree_count==1 && private_copy.token==0x001f8c80 && private_copy.native_result==12);
    reset();detail[14]=FREE0;observe();
    assert((captured.flags&USB_QUEUE_ANCHOR_FREE) && !(captured.flags&USB_QUEUE_ANCHOR_LIVE) && captured.anchor==FREE0);
    reset();WORD(LIVE1+68)=FREE0;detail[14]=FREE0;observe();
    assert((captured.flags&(USB_QUEUE_ANCHOR_FREE|USB_QUEUE_ANCHOR_LIVE))==
        (USB_QUEUE_ANCHOR_FREE|USB_QUEUE_ANCHOR_LIVE));
    reset();WORD(HEAD+68)=0xfffff000;observe();assert(captured.flags&USB_QUEUE_BAD_LIVE);
    reset();detail[12]=0xfffff000;observe();assert(captured.flags&USB_QUEUE_BAD_FREE);
    reset();WORD(LIVE1+68)=LIVE0;observe();assert((captured.flags&USB_QUEUE_LIVE_TRUNCATED) && captured.live_nodes==16);
    reset();WORD(FREE1+68)=FREE0;observe();assert((captured.flags&USB_QUEUE_FREE_TRUNCATED) && captured.free_nodes==16);
    reset();WORD(LIVE1+16)=0xfffff000;observe();assert(!(captured.flags&USB_QUEUE_ANCHOR_QTD_VALID));
    reset();detail[14]=0x10004000;WORD(LIVE1+76)=0x10004000;observe();assert(captured.anchor==LIVE1);
    reset();phy[0]=0;observe();assert(!(captured.flags&USB_QUEUE_MMIO) && !captured.async_list && !captured.anchor);
    reset();api.classic_cx=true;observe();assert(captures==1 && !captured.flags && !phy_reads);
    reset();resolved=false;observe();assert(captures==1 && !captured.flags && !phy_reads);
    reset();tree_valid=false;observe();assert(captures==1 && !captured.flags && !phy_reads);
    reset();WORD(EVENT+12)=0;observe();assert(captured.event_flags==UINT32_MAX && captured.event_waiters==UINT32_MAX);
    puts("PASS read-only USB queue capture: live/retired anchor membership, physical addresses, qTD tokens, bounded cycles/bad pointers and disabled/unknown gates; all metadata/registers/locks unchanged");
    return 0;
}
