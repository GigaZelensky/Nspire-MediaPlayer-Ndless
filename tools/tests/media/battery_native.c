#include "battery_native.h"
#include "portable_reader_platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BASE 0x10000000U
#define SIZE 0x500000U
static unsigned char *ram;
static size_t bytes;
static uint32_t mask=0x80,reads;
static PortableView active_view;
static bool allow(void *p,uint32_t at,uint32_t n,unsigned kind)
{(void)p;(void)kind;return at>=BASE && n<=bytes && at-BASE<=bytes-n;}
static bool read_word(void *p,uint32_t at,uint32_t *v)
{if((at&3) || !allow(p,at,4,0))return false;memcpy(v,ram+at-BASE,4);++reads;return true;}
static const uint32_t *code_words(void *p,uint32_t at,uint32_t n)
{return allow(p,at,n,0)?(const uint32_t *)(const void *)(ram+at-BASE):NULL;}
static void put(uint32_t at,uint32_t v){assert(allow(NULL,at,4,0));memcpy(ram+at-BASE,&v,4);}
unsigned native_critical_enter(void){unsigned old=mask;mask=0xc0;return old;}
void native_critical_leave(unsigned saved){mask=saved;}
bool portable_reader_platform_refresh(PortableReaderPlatform *p){assert(mask==0xc0);p->view=active_view;return true;}
const PortableView *portable_reader_platform_view(PortableReaderPlatform *p){return &p->view;}
static const uint32_t percent[]={0xe92d4070U,0xe59f406cU,0xe24dd010U,0xe1a06000U,0xe3e05000U,0xe284008cU,0xe3a01801U,0xe3a02000U,0xe28d300cU,0xe58d5000U,0xeb0a8081U,0xe3500000U,0xa000003U,0xe3a04000U,0xe1a00004U,0xe28dd010U,0xe8bd8070U,0xe1a01005U,0xe2840064U,0xeb0a913aU,0xe3500000U,0x1afffff6U,0xe0866086U,0xe0846186U,0xe5d62018U,0xe59f3010U,0xe2840064U,0xe7934102U,0xeb0a90dfU,0xeaffffefU};
static const uint32_t charge[]={0xe92d4010U,0xe59f401cU,0xe3e01000U,0xe1a00004U,0xeb0a8eb7U,0xe1a00004U,0xe5d44028U,0xeb0a8e62U,0xe1a00004U,0xe8bd8010U};
static const uint32_t predicate[]={0xe92d4010U,0xe59f402cU,0xe3e01000U,0xe1a00004U,0xeb0a8eacU,0xe5d43028U,0xe59f0018U,0xe3530007U,0x959f2014U,0x83a04001U,0x97924103U,0xeb0a8e53U,0xe1a00004U,0xe8bd8010U};
static void bl(uint32_t at,uint32_t target){put(at,0xeb000000U|(((target-at-8)/4)&0xffffffU));}
static void code(uint32_t at,const uint32_t *p,unsigned n)
{for(unsigned i=0;i<n;++i)put(at+4*i,p[i]);}
static void resolve(BatteryNative *b)
{
 unsigned turns=0;while(!b->complete){uint32_t before=b->scanned_words;
 battery_native_resolve_step(b,&active_view,511);assert(b->scanned_words-before<=511 && ++turns<10000);}
}
static void synthetic(bool classic)
{
 bytes=SIZE;ram=calloc(1,bytes);assert(ram);
 active_view=(PortableView){NULL,allow,read_word,BASE,BASE+0x400000,BASE,BASE+SIZE,classic?code_words:NULL};
 uint32_t p=BASE+0x2000,c=BASE+0x4000,state=BASE+0x401000,cs=BASE+0x402000;
 uint32_t table=BASE+0x403000,threshold=BASE+0x404000,booleans=BASE+0x405000;
 code(p,percent,30);code(c,charge,10);code(c+44,predicate,14);
 unsigned event=classic?212:140,sem=classic?52:100,index=classic?204:48;
 put(p+20,0xe2840000U|event);put(p+72,0xe2840000U|sem);put(p+104,0xe2840000U|sem);
 put(p+96,classic?0xe5d6206cU:0xe5d62018U);put(p+120,state);put(p+124,table);
 put(c+40,cs);put(c+100,cs);put(c+104,booleans);
 bl(p+40,BASE+0x100);bl(p+76,BASE+0x200);bl(p+112,BASE+0x300);
 bl(c+16,BASE+0x200);bl(c+28,BASE+0x300);bl(c+60,BASE+0x200);bl(c+88,BASE+0x300);
 put(state+sem+12,0x53454d41);put(state+event+12,0x45564e54);put(state+event+24,0x10000);
 put(cs+12,0x53454d41);put(cs+40,1);put(state+index+4,threshold);
 static const uint32_t levels[]={100,75,50,25,10,2,0};
 for(unsigned i=0;i<7;++i){put(table+4*i,levels[i]);put(threshold+8*i,i==6?0:3900-i*100);put(threshold+8*i+4,i?4000-i*100:0);}
 for(unsigned i=0;i<8;++i)put(booleans+4*i,i>=2 && i<=6);
 BatteryNative b;battery_native_init(&b);resolve(&b);BatteryNativeSnapshot s;
 assert(b.percent_matches==1 && b.charge_matches==1 && b.percent_index_offset==index);
 assert(battery_native_snapshot(&b,&active_view,&s) && s.percent==100 && s.charge_mode==1 &&
   s.capabilities==(BATTERY_NATIVE_PERCENT|BATTERY_NATIVE_CHARGE_MODE|BATTERY_NATIVE_THRESHOLDS));
 assert(!(s.capabilities&BATTERY_NATIVE_KNOWN_FULL));
 unsigned incoming[]={0,0x40,0x80,0xc0};
 unsigned char *unchanged=malloc(bytes);assert(unchanged);memcpy(unchanged,ram,bytes);
 for(unsigned trial=0;trial<4;++trial) {
  BatteryNative foreground;battery_native_init(&foreground);mask=incoming[trial];unsigned turns=0;
  while(!foreground.complete) {
   unsigned before=foreground.scanned_words;
   (void)battery_native_default(&foreground,&s,16384);
   assert(foreground.scanned_words-before<=16384 && mask==incoming[trial] && ++turns<=64);
  }
  assert(battery_native_default(&foreground,&s,4096) && s.percent==100 && mask==incoming[trial]);
  assert(!memcmp(unchanged,ram,bytes));
 }
 free(unchanged);mask=0x80;
 BatteryNative limited;battery_native_init(&limited);
 assert(!battery_native_resolve_step(&limited,&active_view,UINT32_MAX) && limited.scanned_words==4096);
 PortableView changed=active_view;changed.code_end-=4;
 assert(battery_native_resolve_step(&limited,&changed,1) && (limited.errors&BATTERY_NATIVE_CHANGED));
 assert(!battery_native_snapshot(&b,NULL,&s) && (s.errors&BATTERY_NATIVE_BAD_VIEW));
 for(unsigned i=0;i<7;++i){put(state+index,i);assert(battery_native_snapshot(&b,&active_view,&s) && s.percent==levels[i]);}
 put(state+index,7);assert(battery_native_snapshot(&b,&active_view,&s) && !(s.capabilities&BATTERY_NATIVE_PERCENT));
 put(state+index,0);put(state+event+24,0);assert(battery_native_snapshot(&b,&active_view,&s) && !(s.capabilities&BATTERY_NATIVE_PERCENT));
 put(state+event+24,0x10000);put(c+100,state);
 assert(battery_native_snapshot(&b,&active_view,&s) && !(s.capabilities&BATTERY_NATIVE_CHARGE_MODE));put(c+100,cs);
 put(table+4,101);assert(battery_native_snapshot(&b,&active_view,&s) && !(s.capabilities&BATTERY_NATIVE_PERCENT));put(table+4,75);
 unsigned before=reads;assert(battery_native_default(&b,&s,32) && s.capabilities && reads>before && mask==0x80);
 /* Read-only foreground capture takes its own exclusion, preserving an
  * incoming enabled-IRQ policy rather than treating it as unavailable data. */
 mask=0;before=reads;assert(battery_native_default(&b,&s,32) && reads>before && mask==0);mask=0x80;
 /* Relocated duplicate of an otherwise authentic getter refuses ambiguity. */
 memcpy(ram+0x30000,ram+0x2000,128);
 bl(BASE+0x30000+40,BASE+0x100);bl(BASE+0x30000+76,BASE+0x200);bl(BASE+0x30000+112,BASE+0x300);
 battery_native_init(&b);resolve(&b);assert(b.percent_matches==2 && (b.errors&BATTERY_NATIVE_AMBIGUOUS) && !(b.capabilities&BATTERY_NATIVE_PERCENT));
 free(ram);
}
static void artifact(const char *path)
{
 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n<=0x4000000);
 bytes=(size_t)n;rewind(f);ram=malloc(bytes);assert(ram && fread(ram,1,bytes,f)==bytes);fclose(f);
 unsigned char *copy=malloc(bytes);assert(copy);memcpy(copy,ram,bytes);
 active_view=(PortableView){NULL,allow,read_word,BASE,BASE+0x400000,BASE,BASE+(uint32_t)bytes,code_words};
 BatteryNative b;battery_native_init(&b);resolve(&b);BatteryNativeSnapshot s;
 bool ok=battery_native_snapshot(&b,&active_view,&s);
 printf("artifact percent=%x charge=%x index_offset=%x caps=%x errors=%x ok=%u pct=%u mode=%u\n",b.percent_entry,b.charge_entry,b.percent_index_offset,s.capabilities,s.errors,ok,s.percent,s.charge_mode);
 assert(ok && (s.capabilities&BATTERY_NATIVE_PERCENT) && (s.capabilities&BATTERY_NATIVE_CHARGE_MODE));
 if(s.capabilities&BATTERY_NATIVE_CALIBRATION) {
  uint32_t handle,state;assert(read_word(NULL,b.adc_global,&handle) && read_word(NULL,handle+76,&state));
  put(state+8,1);put(state+16,1750);put(state+84,2932);put(state+88,3901);put(state+48,1);
  put(state+604,1);put(state+156,950);put(state+4,0);put(state+268,0);
  assert(battery_native_snapshot(&b,&active_view,&s) &&
    (s.capabilities&BATTERY_NATIVE_CACHED_VOLTAGE) && s.voltage_mv==4164);
  uint32_t fingerprint=s.calibration_fingerprint;
  put(state+84,3000);put(state+88,4000);
  assert(battery_native_snapshot(&b,&active_view,&s) && s.calibration_fingerprint!=fingerprint);
  put(state+156,4096);assert(battery_native_snapshot(&b,&active_view,&s) &&
    !(s.capabilities&BATTERY_NATIVE_CACHED_VOLTAGE) && (s.capabilities&BATTERY_NATIVE_PERCENT));
  put(state+88,3000);assert(battery_native_snapshot(&b,&active_view,&s) &&
    !(s.capabilities&BATTERY_NATIVE_CALIBRATION) && (s.capabilities&BATTERY_NATIVE_PERCENT));
  memcpy(ram,copy,bytes);
 }
 assert(!memcmp(copy,ram,bytes));free(copy);free(ram);
}
int main(int argc,char **argv)
{
 synthetic(false);synthetic(true);
 for(int i=1;i<argc;++i)artifact(argv[i]);
 puts("PASS native battery: relocated CX/CXII coarse families, native readiness/mode/thresholds, bounds and ambiguity; no getter calls/writes/full claims");return 0;
}
