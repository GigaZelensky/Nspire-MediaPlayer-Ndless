#include "player/native_usb_timing.h"
#include "player/native_usb_ui_resolver.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define BASE 0x10000000U
#define HUB 0x10a62074U
#define RESET 0x10a64938U
#define DELAY 0x10489bd4U
static uint8_t *data,*original;
static size_t bytes=0x2000000;
static unsigned mask=0x80,writes,syncs,fail_write;
static bool allowed=true;
#ifndef TEST_REAL_RESOLVER
static bool proof=true;
bool native_usb_ui_role_matches(const PortableView *v,uint32_t at,unsigned role)
{return v && proof && ((at==HUB && role==UI_HUB_EXPLORE) || (at==RESET && role==UI_PORT_RESET) ||
    (at==DELAY && role==UI_USB_DELAY));}
#endif
unsigned native_critical_enter(void){unsigned before=mask;mask=0xc0;return before;}
void native_critical_leave(unsigned before){mask=before;}
static bool allow(void *c,uint32_t at,uint32_t n,unsigned kind)
{(void)c;return allowed && at>=BASE && n<=bytes && at-BASE<=bytes-n &&
    (kind!=PORTABLE_CODE || (at<0x11000000U && n<=0x11000000U-at));}
static bool read_word(void *c,uint32_t at,uint32_t *out)
{if(!allow(c,at,4,PORTABLE_DATA))return false;memcpy(out,data+at-BASE,4);return true;}
static uint32_t get(uint32_t at){uint32_t v;memcpy(&v,data+at-BASE,4);return v;}
static void put(uint32_t at,uint32_t value){memcpy(data+at-BASE,&value,4);}
static void write_word(uint32_t at,uint32_t value)
{assert(mask==0xc0);if(++writes!=fail_write)put(at,value);}
static void sync_code(uint32_t first,uint32_t second)
{assert(mask==0xc0 && first==HUB+0x170U && second==RESET+0x2acU);++syncs;}
static NativeUsbTimingIo io={write_word,sync_code};
static PortableView view={NULL,allow,read_word,BASE,0x11000000U,BASE,0x12000000U,NULL};
static void fresh(void)
{
    assert(native_usb_timing_end());memcpy(data,original,bytes);
    writes=syncs=fail_write=0;allowed=true;mask=0x80;
}
int main(int argc,char **argv)
{
    data=calloc(1,bytes);original=malloc(bytes);assert(data && original);
#ifdef TEST_REAL_RESOLVER
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(data,1,bytes,f)==bytes);assert(!fclose(f));
#else
    (void)argc;(void)argv;
    put(HUB+0x170U,0xe3a00f4bU);put(RESET+0x2acU,0xe3a000faU);
    put(HUB+0x174U,0xeb000000U|(((DELAY-(HUB+0x174U+8U))/4U)&0xffffffU));
    put(RESET+0x2b0U,0xeb000000U|(((DELAY-(RESET+0x2b0U+8U))/4U)&0xffffffU));
#endif
    memcpy(original,data,bytes);
    assert(native_usb_ui_role_matches(&view,HUB,UI_HUB_EXPLORE));
    assert(native_usb_ui_role_matches(&view,RESET,UI_PORT_RESET));
    assert(native_usb_timing_begin(&view,HUB,RESET,io) && mask==0x80 && writes==2 && syncs==1);
    assert(get(HUB+0x170U)==0xe3a000b4U && get(RESET+0x2acU)==0xe3a00028U);
    assert(native_usb_timing_original(&view,HUB+0x170U,get(HUB+0x170U))==0xe3a00f4bU);
    PortableView foreign=view;
    assert(native_usb_timing_original(&foreign,HUB+0x170U,get(HUB+0x170U))==0xe3a000b4U);
    assert(native_usb_timing_original(&view,HUB+0x170U,0xe3a00000U)==0xe3a00000U);
    assert(native_usb_timing_begin(&view,HUB,RESET,io) && writes==2);
#ifdef TEST_REAL_RESOLVER
    assert(native_usb_ui_role_matches(&view,HUB,UI_HUB_EXPLORE));
    assert(native_usb_ui_role_matches(&view,RESET,UI_PORT_RESET));
    assert(!native_usb_ui_role_matches(&foreign,HUB,UI_HUB_EXPLORE));
    uint32_t untouched=get(HUB+0x20U);put(HUB+0x20U,untouched^1U);
    assert(!native_usb_ui_role_matches(&view,HUB,UI_HUB_EXPLORE));put(HUB+0x20U,untouched);
#endif
    assert(native_usb_timing_end() && mask==0x80 && !memcmp(data,original,bytes));
    for(unsigned fail=1;fail<=2;++fail) {
        fresh();fail_write=fail;
        assert(!native_usb_timing_begin(&view,HUB,RESET,io) && mask==0x80);
        assert(!memcmp(data,original,bytes) && native_usb_timing_end());
    }
    for(unsigned fail=3;fail<=4;++fail) {
        fresh();assert(native_usb_timing_begin(&view,HUB,RESET,io));fail_write=fail;
        assert(!native_usb_timing_end() && mask==0x80);
        assert(native_usb_timing_end() && mask==0x80 && !memcmp(data,original,bytes));
    }
    for(unsigned bad=0;bad<6;++bad) {
        fresh();
        switch(bad) {
        case 0:put(HUB+0x170U,0xe3a00000U);break;
        case 1:put(RESET+0x2acU,0xe3a00000U);break;
        case 2:put(HUB+0x174U,0xe1a00000U);break;
        case 3:put(RESET+0x2b0U,get(RESET+0x2b0U)+4);break;
        case 4:allowed=false;break;
        case 5:
#ifdef TEST_REAL_RESOLVER
            put(DELAY,get(DELAY)^1U);
#else
            proof=false;
#endif
            break;
        }
        assert(!native_usb_timing_begin(&view,HUB,RESET,io) && !writes && !syncs && mask==0x80);
#ifndef TEST_REAL_RESOLVER
        proof=true;
#endif
    }
    fresh();assert(!native_usb_timing_begin(NULL,HUB,RESET,io) && !writes);
    assert(!native_usb_timing_begin(&view,UINT32_MAX,RESET,io) && !writes);
    assert(!native_usb_timing_begin(&view,HUB,RESET,(NativeUsbTimingIo){0}) && !writes);
#ifndef TEST_REAL_RESOLVER
    proof=false;assert(!native_usb_timing_begin(&view,HUB,RESET,io) && !writes);proof=true;
#endif
    fresh();assert(native_usb_timing_begin(&view,HUB,RESET,io));
    put(HUB+0x170U,0xe1a00000U);assert(!native_usb_timing_end());
    assert(get(HUB+0x170U)==0xe1a00000U && get(RESET+0x2acU)==0xe3a000faU);
    fresh();assert(!memcmp(data,original,bytes));
    puts("PASS guarded native timing: exact owned instructions only, unrelated/foreign changes refused, IRQ mask preserved, write failures rolled back, complete restoration");
    free(original);free(data);return 0;
}
