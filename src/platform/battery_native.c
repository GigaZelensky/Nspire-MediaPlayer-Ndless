#include "battery_native.h"
#include <string.h>
#include <limits.h>

static const uint32_t percent_shape[]={
    0xe92d4070U,0xe59f406cU,0xe24dd010U,0xe1a06000U,0xe3e05000U,
    0xe284008cU,0xe3a01801U,0xe3a02000U,0xe28d300cU,0xe58d5000U,
    0xeb0a8081U,0xe3500000U,0x0a000003U,0xe3a04000U,0xe1a00004U,
    0xe28dd010U,0xe8bd8070U,0xe1a01005U,0xe2840064U,0xeb0a913aU,
    0xe3500000U,0x1afffff6U,0xe0866086U,0xe0846186U,0xe5d62018U,
    0xe59f3010U,0xe2840064U,0xe7934102U,0xeb0a90dfU,0xeaffffefU,
};
static const uint32_t charge_shape[]={
    0xe92d4010U,0xe59f401cU,0xe3e01000U,0xe1a00004U,0xeb0a8eb7U,
    0xe1a00004U,0xe5d44028U,0xeb0a8e62U,0xe1a00004U,0xe8bd8010U,
};
static const uint32_t charge_predicate[]={
    0xe92d4010U,0xe59f402cU,0xe3e01000U,0xe1a00004U,0xeb0a8eacU,
    0xe5d43028U,0xe59f0018U,0xe3530007U,0x959f2014U,0x83a04001U,
    0x97924103U,0xeb0a8e53U,0xe1a00004U,0xe8bd8010U,
};
static const uint32_t adc_global_shape[]={
    0xe92d4010U,0xe59f401cU,0xe5940000U,0xe3500000U,0x18bd8010U,
    0xe3e01000U,0xe3a00004U,0xeb01c601U,0xe5840000U,0xe8bd8010U,
};
static const uint32_t converter_shape[]={
    0xe92d4038U,0xe590404cU,0xe1a05001U,0xebffffe1U,0xe0843105U,
    0xe593200cU,0xe59f307cU,0xe5d41008U,0xe3510001U,0xe0020290U,
    0xe0820293U,0xe1a02322U,0x0a000012U,0xe0821102U,0xe081e101U,
    0xe08e038eU,0xe280cf7dU,0xe0c3139cU,0xe1a02fccU,0xe0622343U,
    0xe2855009U,0xe0844185U,0xe594e004U,0xe594c008U,0xe06e2002U,
    0xe3a00ffaU,0xe0000092U,0xe06e100cU,0xeb13cc87U,0xe2803ebbU,
    0xe2830008U,0xe8bd8038U,0xe59f1018U,0xe0020291U,0xe282ef7dU,
    0xe0cc0e93U,0xe1a00fceU,0xe060234cU,0xeaffffecU,0x10624dd3U,
    0x0000098bU,
};

static bool view_ok(const PortableView *v)
{
    return v && v->allow_span && v->read_word && !(v->code_begin&3U) &&
        v->code_begin<v->code_end && v->ram_begin<v->ram_end;
}
static bool span(const PortableView *v,uint32_t at,uint32_t bytes,unsigned kind)
{
    uint32_t first=kind==PORTABLE_CODE?v->code_begin:v->ram_begin;
    uint32_t last=kind==PORTABLE_CODE?v->code_end:v->ram_end;
    return !(at&3U) && bytes && at>=first && bytes<=last-first && at<=last-bytes &&
        v->allow_span(v->context,at,bytes,kind);
}
static bool word(const PortableView *v,uint32_t at,unsigned kind,uint32_t *out)
{return span(v,at,4,kind) && v->read_word(v->context,at,out);}
static bool literal(const PortableView *v,uint32_t at,uint32_t *out)
{
    uint32_t w;if(!word(v,at,PORTABLE_CODE,&w) || (w&0x0f7f0000U)!=0x051f0000U)return false;
    int64_t p=(int64_t)at+8+((w&(1U<<23))?(int32_t)(w&4095U):-(int32_t)(w&4095U));
    return p>=0 && p<=UINT32_MAX && word(v,(uint32_t)p,PORTABLE_CODE,out);
}
static bool call(const PortableView *v,uint32_t at,uint32_t *out)
{
    uint32_t w;if(!word(v,at,PORTABLE_CODE,&w) || (w&0xff000000U)!=0xeb000000U)return false;
    int32_t d=(int32_t)(w&0xffffffU);if(d&0x800000)d-=0x1000000;
    int64_t target=(int64_t)at+8+4LL*d;
    if(target<0 || target>UINT32_MAX || !span(v,(uint32_t)target,4,PORTABLE_CODE))return false;
    *out=(uint32_t)target;return true;
}
static bool shape(const PortableView *v,uint32_t at,const uint32_t *p,unsigned n,bool percent)
{
    if(!span(v,at,n*4U,PORTABLE_CODE))return false;
    for(unsigned i=0;i<n;++i) {
        uint32_t w,mask=UINT32_MAX;
        if(!v->read_word(v->context,at+i*4U,&w))return false;
        if((p[i]&0xff000000U)==0xeb000000U)mask=0xff000000U;
        else if((p[i]&0x0f7f0000U)==0x051f0000U)mask=0xfffff000U;
        if(percent && (i==5 || i==18 || i==24 || i==26))mask=0xfffff000U;
        if((w&mask)!=(p[i]&mask))return false;
    }
    return true;
}
static bool levels(const PortableView *v,uint32_t at,uint32_t out[7])
{
    if(!span(v,at,28,PORTABLE_DATA))return false;
    for(unsigned i=0;i<7;++i)
        if(!v->read_word(v->context,at+4*i,out+i) || out[i]>100 || (i && out[i]>=out[i-1]))return false;
    return out[0]==100 && out[6]==0;
}
static bool percent_binding(BatteryNative *b,const PortableView *v,uint32_t at)
{
    uint32_t ev,sem,field,sem2,state,table,take,give,values[7];
    if(!shape(v,at,percent_shape,30,true) || !literal(v,at+4,&state) ||
       !literal(v,at+100,&table) || !levels(v,table,values) ||
       !call(v,at+76,&take) || !call(v,at+112,&give) ||
       !word(v,at+20,PORTABLE_CODE,&ev) || !word(v,at+72,PORTABLE_CODE,&sem) ||
       !word(v,at+96,PORTABLE_CODE,&field) || !word(v,at+104,PORTABLE_CODE,&sem2) || sem!=sem2)return false;
    ev&=4095U;sem&=4095U;field&=4095U;
    unsigned index;
    if(ev==140 && sem==100 && field==24)index=1;
    else if(ev==212 && sem==52 && field==108)index=4;
    else return false;
    if(!span(v,state,ev+36,PORTABLE_DATA))return false;
    if(!b->percent_matches) {
        b->percent_entry=at;b->percent_state=state;b->percent_table=table;
        b->percent_index_offset=field+24U*index;b->percent_semaphore_offset=sem;
        b->percent_event_offset=ev;b->percent_take=take;b->percent_give=give;
    }
    if(b->percent_matches<2)++b->percent_matches;
    return true;
}
static void charge_binding(BatteryNative *b,const PortableView *v,uint32_t at)
{
    uint32_t state,state2,table,take,give,take2,give2;
    if(!shape(v,at,charge_shape,10,false) || !shape(v,at+44,charge_predicate,14,false) ||
       !literal(v,at+4,&state) || !literal(v,at+48,&state2) || state!=state2 ||
       !literal(v,at+76,&table) || !span(v,state,44,PORTABLE_DATA) ||
       !call(v,at+16,&take) || !call(v,at+28,&give) || !call(v,at+60,&take2) ||
       !call(v,at+88,&give2) || take!=take2 || give!=give2)return;
    for(unsigned i=0;i<8;++i) {uint32_t w;
        if(!word(v,table+4*i,PORTABLE_DATA,&w) || w!=(i>=2 && i<=6))return;
    }
    if(!b->charge_matches) {b->charge_entry=at;b->charge_state=state;b->charge_take=take;b->charge_give=give;}
    if(b->charge_matches<2)++b->charge_matches;
}
void battery_native_init(BatteryNative *b)
{if(b)memset(b,0,sizeof(*b));}
bool battery_native_resolve_step(BatteryNative *b,const PortableView *v,unsigned budget)
{
    if(!b || !view_ok(v)) {if(b)b->errors|=BATTERY_NATIVE_BAD_VIEW;return true;}
    if(b->complete)return true;
    if(!b->initialized) {
        b->initialized=true;b->scan_next=v->code_begin;
        b->view_begin=v->code_begin;b->view_end=v->code_end;
        uint32_t bytes=v->code_end-v->code_begin;
        b->scan_end=v->code_begin+(bytes>0x400000U?0x400000U:bytes);
    }
    if(b->view_begin!=v->code_begin || b->view_end!=v->code_end) {
        b->errors|=BATTERY_NATIVE_CHANGED;b->capabilities=0;b->complete=true;return true;
    }
    if(budget>4096)budget=4096;
    const uint32_t *mapped=NULL;unsigned available=0;
    while(budget-- && b->scan_next<=b->scan_end-4U) {
        uint32_t at=b->scan_next,w;b->scan_next+=4;++b->scanned_words;
        if(!available) {
            unsigned n=budget+1U;if(n>256)n=256;
            if(n>(b->scan_end-at)/4U)n=(b->scan_end-at)/4U;
            mapped=v->code_words && span(v,at,n*4U,PORTABLE_CODE)?
                v->code_words(v->context,at,n*4U):NULL;
            available=n;
        }
        --available;
        if(mapped)w=*mapped++;
        else if(!word(v,at,PORTABLE_CODE,&w))continue;
        if(w==percent_shape[0]) (void)percent_binding(b,v,at);
        else if(w==charge_shape[0]) {
            uint32_t global;
            charge_binding(b,v,at);
            if(shape(v,at,adc_global_shape,10,false) && literal(v,at+4,&global) &&
               span(v,global,4,PORTABLE_DATA)) {
                if(!b->adc_matches){b->adc_entry=at;b->adc_global=global;}
                if(b->adc_matches<2)++b->adc_matches;
            }
        } else if(w==converter_shape[0] && shape(v,at,converter_shape,41,false)) {
            if(!b->converter_matches)b->converter_entry=at;
            if(b->converter_matches<2)++b->converter_matches;
        }
    }
    if(b->scan_next<=b->scan_end-4U)return false;
    b->complete=true;
    if(b->percent_matches==1)b->capabilities|=BATTERY_NATIVE_PERCENT;
    if(b->charge_matches==1 && b->percent_matches==1 && b->charge_take==b->percent_take &&
       b->charge_give==b->percent_give)b->capabilities|=BATTERY_NATIVE_CHARGE_MODE;
    if(b->adc_matches==1 && b->converter_matches==1)b->capabilities|=BATTERY_NATIVE_CALIBRATION;
    if(b->percent_matches>1 || b->charge_matches>1 || b->adc_matches>1 || b->converter_matches>1)
        b->errors|=BATTERY_NATIVE_AMBIGUOUS;
    return true;
}
static bool byte(const PortableView *v,uint32_t at,uint32_t *out)
{uint32_t w;if(!word(v,at&~3U,PORTABLE_DATA,&w))return false;*out=(w>>((at&3U)*8U))&255U;return true;}
static void adc_snapshot(const BatteryNative *b,const PortableView *v,BatteryNativeSnapshot *s)
{
    uint32_t handle,state,id,type,entry,mode,ref;
    if(!shape(v,b->adc_entry,adc_global_shape,10,false) ||
       !shape(v,b->converter_entry,converter_shape,41,false) ||
       !word(v,b->adc_global,PORTABLE_DATA,&handle) || !span(v,handle,132,PORTABLE_DATA) ||
       !word(v,handle+68,PORTABLE_DATA,&id) || id!=0x494f4452U ||
       !word(v,handle+100,PORTABLE_DATA,&type) || type!=4 ||
       !word(v,handle+112,PORTABLE_DATA,&entry) || !span(v,entry,388,PORTABLE_CODE) ||
       !word(v,handle+76,PORTABLE_DATA,&state) || !span(v,state,608,PORTABLE_DATA) ||
       !byte(v,state+8,&mode) || mode>1 ||
       !word(v,state+16,PORTABLE_DATA,&s->scale) || s->scale<100 || s->scale>10000 ||
       !word(v,state+84,PORTABLE_DATA,&s->calibration_low) ||
       !word(v,state+88,PORTABLE_DATA,&s->calibration_high) ||
       s->calibration_high<=s->calibration_low || s->calibration_high>10000 ||
       !word(v,state+48,PORTABLE_DATA,&s->mapped_channel) || s->mapped_channel>=8)return;
    /* Cached ADC metadata is a separately proven6.2 driver family. Coarse
     * fallback is broader; classic CX's native ADC getter actively samples
     * and is intentionally never invoked or treated as this cache layout. */
    uint32_t driver_hash=2166136261U;
    for(unsigned i=0;i<388;i+=4) {
        uint32_t w;if(!v->read_word(v->context,entry+i,&w))return;
        for(unsigned j=0;j<4;++j)driver_hash=(driver_hash^((w>>(8*j))&255U))*16777619U;
    }
    if(driver_hash!=0x0bf56b99U)return;
    ref=mode==1?2443:3225;s->reference_mv=ref;
    const uint32_t fields[]={s->scale,ref,s->calibration_low,s->calibration_high,s->mapped_channel};
    uint32_t hash=2166136261U;
    for(unsigned i=0;i<5;++i)hash=(hash^fields[i])*16777619U;
    s->calibration_fingerprint=hash;s->capabilities|=BATTERY_NATIVE_CALIBRATION;
    uint32_t ready,stamp;
    if(!word(v,state+604,PORTABLE_DATA,&ready) || !ready ||
       !word(v,state+4,PORTABLE_DATA,&s->busy) || !word(v,state+268,PORTABLE_DATA,&s->count) ||
       !word(v,state+272,PORTABLE_DATA,&stamp) ||
       !word(v,state+140+16*s->mapped_channel,PORTABLE_DATA,&s->raw) || s->raw>4095)return;
    s->batch_stamp=stamp&65535U;s->batch_duration=stamp>>16;
    uint32_t nominal=((s->raw*s->scale/1000U)*ref+500U)/1000U;
    if(nominal<s->calibration_low)return;
    uint32_t mv=3000U+(uint64_t)1000U*(nominal-s->calibration_low)/
        (s->calibration_high-s->calibration_low);
    if(mv>=2500 && mv<=4500){s->voltage_mv=mv;s->capabilities|=BATTERY_NATIVE_CACHED_VOLTAGE;}
}
bool battery_native_snapshot(const BatteryNative *b,const PortableView *v,BatteryNativeSnapshot *s)
{
    if(!s)return false;
    memset(s,0,sizeof(*s));
    if(!b || !view_ok(v)){s->errors=BATTERY_NATIVE_BAD_VIEW;return false;}
    s->errors=b->errors;if(!b->complete)return false;
    if(b->view_begin!=v->code_begin || b->view_end!=v->code_end) {
        s->errors|=BATTERY_NATIVE_CHANGED;return false;
    }
    BatteryNative checked={0};
    bool same_percent=percent_binding(&checked,v,b->percent_entry) &&
        checked.percent_state==b->percent_state && checked.percent_table==b->percent_table &&
        checked.percent_index_offset==b->percent_index_offset;
    uint32_t id,flags;
    if((b->capabilities&BATTERY_NATIVE_PERCENT) && same_percent &&
       word(v,b->percent_state+b->percent_semaphore_offset+12,PORTABLE_DATA,&id) && id==0x53454d41U &&
       word(v,b->percent_state+b->percent_event_offset+12,PORTABLE_DATA,&id) && id==0x45564e54U &&
       word(v,b->percent_state+b->percent_event_offset+24,PORTABLE_DATA,&flags) && (flags&0x10000U) &&
       levels(v,b->percent_table,s->levels) && byte(v,b->percent_state+b->percent_index_offset,&s->level_index) &&
       s->level_index<7) {
        s->percent=s->levels[s->level_index];s->capabilities|=BATTERY_NATIVE_PERCENT;
        uint32_t table;
        if(word(v,b->percent_state+b->percent_index_offset+4,PORTABLE_DATA,&table)) {
            bool ok=true;
            for(unsigned i=0;i<7 && ok;++i)for(unsigned j=0;j<2 && ok;++j)
                ok=word(v,table+8*i+4*j,PORTABLE_DATA,&s->thresholds[i][j]) &&
                    s->thresholds[i][j]<=20000 && (!i || j || s->thresholds[i][0]<=s->thresholds[i-1][0]);
            for(unsigned i=0;i<7 && ok;++i) {
                if(!i)ok=s->thresholds[0][0]>0;
                else ok=s->thresholds[i][0]<s->thresholds[i-1][0] &&
                    s->thresholds[i][1]>=s->thresholds[i][0] && s->thresholds[i][1]>0 &&
                    (i==1 || s->thresholds[i][1]<=s->thresholds[i-1][1]);
            }
            if(ok && !s->thresholds[6][0])s->capabilities|=BATTERY_NATIVE_THRESHOLDS;
        }
    }
    checked=(BatteryNative){0};charge_binding(&checked,v,b->charge_entry);
    if((b->capabilities&BATTERY_NATIVE_CHARGE_MODE) && checked.charge_matches==1 &&
       checked.charge_state==b->charge_state &&
       word(v,b->charge_state+12,PORTABLE_DATA,&id) && id==0x53454d41U &&
       byte(v,b->charge_state+40,&s->charge_mode) && s->charge_mode<8)
        s->capabilities|=BATTERY_NATIVE_CHARGE_MODE;
    if(b->capabilities&BATTERY_NATIVE_CALIBRATION)adc_snapshot(b,v,s);
    return s->capabilities!=0;
}

#if defined(__arm__)
#include "portable_reader_platform.h"
#include "nspire_hardware.h"
#include "../player/native_interrupts.h"
bool battery_native_default(BatteryNative *b,BatteryNativeSnapshot *s,unsigned budget)
{
    if(!b || !s)return false;
    static PortableReaderPlatform platform;
    unsigned mask=native_critical_enter();bool ok=false;
    if(portable_reader_platform_refresh(&platform)) {
        const PortableView *v=portable_reader_platform_view(&platform);
        /* Four small read-only slices during initial discovery; the explicit
         * foreground caller bounds the total. CPU exclusion is established
         * above, regardless of the incoming IRQ policy. No getter is called. */
        if(budget>16384U)budget=16384U;
        do {
            unsigned slice=budget>4096U?4096U:budget;
            battery_native_resolve_step(b,v,slice);
            budget-=slice;
        } while(budget && !b->complete);
        ok=battery_native_snapshot(b,v,s);
    } else if(s)memset(s,0,sizeof(*s));
    native_critical_leave(mask);return ok;
}
#else
bool battery_native_default(BatteryNative *b,BatteryNativeSnapshot *s,unsigned budget)
{(void)b;(void)budget;if(s)memset(s,0,sizeof(*s));return false;}
#endif
