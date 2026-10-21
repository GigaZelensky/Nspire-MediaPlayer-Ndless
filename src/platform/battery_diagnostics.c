#include "battery_diagnostics.h"
#if NDVIDEO_BATTERY_TIMINGS || NDMEDIA_BATTERY_SERVICE
#include "nspire_hardware.h"
#include "../player/native_interrupts.h"
#include <stdbool.h>
#include <string.h>
#if NDVIDEO_BATTERY_ACQUISITION || NDMEDIA_BATTERY_SERVICE
#include "battery_adc_poll.h"
#include "../sram.h"
#endif

enum { FIRST_SAMPLES=NDVIDEO_BATTERY_TIMINGS?8:1, RECENT_SAMPLES=NDVIDEO_BATTERY_TIMINGS?24:1, SAMPLE_PERIOD_MS=5000 };
typedef struct {
    uint32_t now_ms,rtc,irq,os_percent,source;
    uint32_t raw,trimmed_sum,maximum,minimum,millivolts,valid_voltage;
    uint32_t mode,busy,count,bypass,batch_stamp,batch_duration;
    uint32_t handle,state,channel,scale,reference,low,high;
} BatterySample;
static BatterySample samples[FIRST_SAMPLES+RECENT_SAMPLES];
static uint32_t samples_seen,last_sample_ms,signature_failure;
static bool initialized,supported;
#if NDVIDEO_BATTERY_ACQUISITION || NDMEDIA_BATTERY_SERVICE
typedef struct {
    uint32_t now_ms,guard,irq,fresh_mv,cache_unchanged;
    BatterySample cached;
    AdcPollResult adc;
} FreshSample;
static FreshSample fresh_samples[NDVIDEO_BATTERY_TIMINGS?16:1];
static uint32_t fresh_seen,fresh_last_ms;
static bool acquiring,acquisition_disabled;
#endif
static uint32_t read32(uint32_t at) { return *(const volatile uint32_t *)(uintptr_t)at; }
static uint32_t read16(uint32_t at) { return *(const volatile uint16_t *)(uintptr_t)at; }
static uint32_t read8(uint32_t at) { return *(const volatile uint8_t *)(uintptr_t)at; }
static bool ram(uint32_t at,unsigned bytes)
{ return !(at&3U) && at>=0x10000000U && at<0x14000000U && bytes<=0x14000000U-at; }
static uint32_t fingerprint(uint32_t at,unsigned bytes)
{
    uint32_t hash=2166136261U;
    const volatile unsigned char *p=(const volatile unsigned char *)(uintptr_t)at;
    for(unsigned i=0;i<bytes;++i)hash=(hash^p[i])*16777619U;
    return hash;
}
static bool exact_firmware(void)
{
    static const struct { uint32_t at,bytes,hash; } shapes[]={
        {0x10003974U,44,0xa6b5d862U}, {0x10004600U,164,0xc888636fU},
        {0x100048c4U,388,0x0bf56b99U}, {0x10102c18U,128,0x854af231U},
        {0x10103660U,44,0x54141d1aU}, {0x10b189c0U,28,0x43d38119U},
        {0x10003c30U,852,0xe0fdecb9U}, {0x100041e8U,196,0xe612d90fU},
#if NDVIDEO_BATTERY_ACQUISITION || NDMEDIA_BATTERY_SERVICE
        {0x10003b20U,176,0xc05e4212U},{0x10003becU,68,0x8a4d30f3U},
        {0x10003f84U,116,0x302061c9U},{0x10001df8U,44,0x16b617e8U},
        {0x10001474U,64,0x7629d87cU},{0x10001528U,64,0xea844c2aU},
        {0x10001d3cU,80,0x9369a2f9U},{0x10001730U,1528,0x91870bbfU},
#endif
    };
    if(!nspire_is_cx2())return false;
    for(unsigned i=0;i<sizeof(shapes)/sizeof(*shapes);++i)
        if(fingerprint(shapes[i].at,shapes[i].bytes)!=shapes[i].hash) {
            signature_failure=i+1;return false;
        }
    return true;
}
void battery_diagnostics_init(void)
{
    memset(samples,0,sizeof(samples));samples_seen=last_sample_ms=signature_failure=0;
    supported=exact_firmware();initialized=true;
#if NDVIDEO_BATTERY_ACQUISITION || NDMEDIA_BATTERY_SERVICE
    memset(fresh_samples,0,sizeof(fresh_samples));fresh_seen=fresh_last_ms=0;acquiring=acquisition_disabled=false;
#endif
}
static BatterySample capture(uint32_t now_ms)
{
    BatterySample s={.now_ms=now_ms,.os_percent=UINT32_MAX};
    unsigned mask=native_critical_enter();
    s.irq=mask;s.rtc=read32(0x90090000U);
    unsigned index=read32(0x113ebd68U+48)&255U;
    if(index<7)s.os_percent=read32(0x10b189c0U+4*index);
    s.source=read8(0x113ebf10U+40);
    uint32_t handle=read32(0x113c10d0U);
    uint32_t scale=0,reference=0,low=0,high=0;
    bool valid_channel=false;
    if(ram(handle,132) && read32(handle+68)==0x494f4452U &&
        read32(handle+100)==4 && read32(handle+112)==0x100048c4U) {
        uint32_t state=read32(handle+76);
        if(ram(state,608)) {
            s.state=state;
            s.mode=read8(state);s.busy=read32(state+4);s.count=read32(state+268);
            s.bypass=read32(state+604);
            s.batch_stamp=read16(state+272);s.batch_duration=read16(state+274);
            scale=read32(state+16);low=read32(state+84);high=read32(state+88);
            reference=read8(state+8)==1?2443:3225;
            uint32_t channel=read32(state+48);
            if(channel<8) {
                valid_channel=true;
                s.channel=channel;
                uint32_t slot=state+140+16*channel;
                s.raw=read32(slot);s.trimmed_sum=read32(slot+4);
                s.maximum=read32(slot+8);s.minimum=read32(slot+12);
            }
        }
    }
    native_critical_leave(mask);
    s.handle=handle;s.scale=scale;s.reference=reference;s.low=low;s.high=high;
    /* Verified against execution of the native integer conversion routine.
     * Never substitute dump/default calibration for this device's values. */
    if(valid_channel && s.raw<=4095 && scale>=100 && scale<=10000 && high>low && high<=10000) {
        uint32_t nominal=((s.raw*scale/1000)*reference+500)/1000;
        if(nominal>=low) {
            uint32_t mv=3000+(uint64_t)1000*(nominal-low)/(high-low);
            if(mv>=2500 && mv<=4500) {s.millivolts=mv;s.valid_voltage=1;}
        }
    }
    return s;
}
#if NDVIDEO_BATTERY_ACQUISITION || NDMEDIA_BATTERY_SERVICE
static uint32_t adc_read(void *context,uint32_t at){(void)context;return read32(at);}
static void adc_write(void *context,uint32_t at,uint32_t value)
{(void)context;*(volatile uint32_t *)(uintptr_t)at=value;}
static void adc_barrier(void *context)
{(void)context;__asm__ volatile("mcr p15,0,%0,c7,c10,4"::"r"(0):"memory");}
static bool peripheral_mapping(void)
{
    uint32_t control,ttbr;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(control));
    __asm__ volatile("mrc p15,0,%0,c2,c0,0":"=r"(ttbr));ttbr&=0xffffc000U;
    if(!(control&1U) || !((ttbr>=0x10000000U && ttbr<=0x13ffc000U) ||
        (ttbr>=0xa4000000U && ttbr<=0xa400c000U)))return false;
    uint32_t table=ttbr;
    if(sram_uses_native_clone()) {
        if(ttbr!=(sram_expected_ttbr()&0xffffc000U) || ttbr<0x10000000U || ttbr>0x13ffc000U)return false;
        uint32_t alias=read32(0xa4004000U+0xa40U*4U);
        if((alias&0x1e3U)!=2 || !(alias&0xc00U) || (alias&0xfff00000U)+0x4000U!=ttbr)return false;
        table=0xa4004000U;
    }
    uint32_t self=read32(table+(ttbr>>20)*4U);
    if((self&0x1e3U)!=2 || !(self&0xc00U) || (self&0xfff00000U)!=(ttbr&0xfff00000U))return false;
    for(unsigned section=0x900;section<=0x901;++section) {
        uint32_t descriptor=read32(table+section*4U);
        if((descriptor&0x1e3U)!=2 || !(descriptor&0xc00U) || (descriptor&0xfff00000U)!=(section<<20))return false;
    }
    return true;
}
static uint32_t state_hash(uint32_t at)
{
    uint32_t hash=2166136261U;
    for(unsigned i=0;i<608;i+=4)hash=(hash^read32(at+i))*16777619U;
    return hash;
}
bool battery_diagnostics_acquisition_due(uint32_t now_ms)
{
    return initialized && supported && !acquiring && !acquisition_disabled &&
        (!fresh_seen || (uint32_t)(now_ms-fresh_last_ms)>=SAMPLE_PERIOD_MS);
}
bool battery_diagnostics_acquire(uint32_t now_ms)
{
    if(!battery_diagnostics_acquisition_due(now_ms))return false;
    acquiring=true;fresh_last_ms=now_ms;
    unsigned slot=NDVIDEO_BATTERY_TIMINGS?(fresh_seen<8?fresh_seen:8U+(fresh_seen-8U)%8U):0;
    FreshSample *f=fresh_samples+slot;memset(f,0,sizeof(*f));f->now_ms=now_ms;
    f->adc.result=ADC_POLL_ARGUMENT;
    unsigned saved=native_critical_enter();f->irq=saved;
    f->cached=capture(now_ms);
    const BatterySample *s=&f->cached;
    /* Only the explicit application foreground may borrow this peripheral.
     * Native callbacks and storage/audio service callbacks are excluded. */
    f->guard=6;if(saved!=0x80U)goto done;
    f->guard=2;if(!peripheral_mapping())goto done;
    f->guard=3;
    if(!s->valid_voltage || !ram(s->state,608) || s->mode || s->busy || s->count ||
        !s->bypass || s->channel>=8 || !ram(s->handle,132) || !read32(s->handle+12))goto done;
    f->guard=4;
    uint32_t pmu=read32(0x113c3e20U);
    if(!ram(pmu,132) || read32(pmu+68)!=0x494f4452U || read32(pmu+100)!=15 ||
        read32(pmu+112)!=0x10001730U)goto done;
    uint32_t state=read32(pmu+76);
    if(!ram(state,32) || read8(state+4)!=0x14U)goto done;
    uint32_t before=state_hash(s->state);
    adc_poll((AdcPollBus){NULL,adc_read,adc_write,adc_barrier},s->channel,s->reference==2443?1:0,&f->adc);
    f->cache_unchanged=state_hash(s->state)==before;
    f->guard=f->cache_unchanged?0:7;
    if(!f->cache_unchanged || (f->adc.touched && !f->adc.restored))acquisition_disabled=true;
    if(!f->guard && f->adc.result==ADC_POLL_OK) {
        uint32_t nominal=((f->adc.raw*s->scale/1000)*s->reference+500)/1000;
        if(nominal>=s->low && s->high>s->low) {
            uint32_t mv=3000+(uint64_t)1000*(nominal-s->low)/(s->high-s->low);
            if(mv>=2500 && mv<=4500)f->fresh_mv=mv;
        }
    }
done:
    native_critical_leave(saved);
    if(fresh_seen!=UINT32_MAX)++fresh_seen;
    acquiring=false;return true;
}
bool battery_diagnostics_latest(BatteryFreshReading *reading)
{
    if(!reading || !fresh_seen || acquiring)return false;
    unsigned index=fresh_seen-1U;
    const FreshSample *f=fresh_samples+(NDVIDEO_BATTERY_TIMINGS?(index<8?index:8U+(index-8U)%8U):0);
    *reading=(BatteryFreshReading){
        .sequence=fresh_seen,.now_ms=f->now_ms,.rtc=f->cached.rtc,
        .millivolts=f->fresh_mv,.cached_mv=f->cached.millivolts,.raw=f->adc.raw,
        .os_percent=f->cached.os_percent,.charge_mode=f->cached.source,
        .guard=f->guard,.result=f->adc.result,.restored=f->adc.restored,
        .cache_unchanged=f->cache_unchanged,
        .valid=!f->guard && f->adc.result==ADC_POLL_OK && f->adc.restored &&
            f->cache_unchanged && f->adc.conversions==10 && f->fresh_mv>=2500 && f->fresh_mv<=4500
    };
    return true;
}
#if NDVIDEO_BATTERY_TIMINGS
static void print_fresh(FILE *file,unsigned slot)
{
    const FreshSample *f=fresh_samples+slot;const AdcPollResult *a=&f->adc;
    fprintf(file,"battery_acquire at_ms=%lu irq=%02lx guard=%lu result=%u conversions=%u "
        "cached_mv=%lu fresh_mv=%lu raw=%lu restored=%u cache_unchanged=%lu polls=%u "
        "timeout=%lu ticks=%lu control=%08lx/%08lx source=%08lx/%08lx\n",
        (unsigned long)f->now_ms,(unsigned long)f->irq,(unsigned long)f->guard,a->result,a->conversions,
        (unsigned long)f->cached.millivolts,(unsigned long)f->fresh_mv,(unsigned long)a->raw,a->restored,
        (unsigned long)f->cache_unchanged,a->polls,(unsigned long)a->timeout_reason,
        (unsigned long)((a->start_ticks-a->end_ticks)&65535U),
        (unsigned long)a->timer_control_before,(unsigned long)a->timer_control_after,
        (unsigned long)a->timer_source_before,(unsigned long)a->timer_source_after);
    fprintf(file,"battery_adc at_ms=%lu gate=%08lx/%08lx control=%08lx/%08lx ref=%08lx/%08lx "
        "irq=%08lx/%08lx status=%08lx/%08lx first_status=%08lx last_status=%08lx retries=%lu\n",
        (unsigned long)f->now_ms,(unsigned long)a->gate_before,(unsigned long)a->gate_after,
        (unsigned long)a->control_before,(unsigned long)a->control_after,
        (unsigned long)a->reference_before,(unsigned long)a->reference_after,
        (unsigned long)a->interrupt_before,(unsigned long)a->interrupt_after,
        (unsigned long)a->status_before,(unsigned long)a->status_after,
        (unsigned long)a->first_status,(unsigned long)a->last_status,(unsigned long)a->error_retries);
}
static void fresh_debug(FILE *file)
{
    unsigned first=fresh_seen<8?fresh_seen:8U;
    uint32_t tail=fresh_seen>8?fresh_seen-8:0;
    unsigned recent=tail<8?tail:8U,oldest=tail>8?tail%8U:0;
    fprintf(file,"battery_acquisitions seen=%lu kept=%u period_ms=%u disabled=%u timer_budget_ticks=256 stagnant_poll_limit=4096\n",
        (unsigned long)fresh_seen,first+recent,SAMPLE_PERIOD_MS,acquisition_disabled);
    for(unsigned i=0;i<first;++i)print_fresh(file,i);
    for(unsigned i=0;i<recent;++i)print_fresh(file,8U+(oldest+i)%8U);
}
#endif
#endif
void battery_diagnostics_tick(uint32_t now_ms)
{
    if(!initialized || !supported ||
        (samples_seen && (uint32_t)(now_ms-last_sample_ms)<SAMPLE_PERIOD_MS))return;
    last_sample_ms=now_ms;
    unsigned slot=NDVIDEO_BATTERY_TIMINGS?(samples_seen<FIRST_SAMPLES?samples_seen:
        FIRST_SAMPLES+(samples_seen-FIRST_SAMPLES)%RECENT_SAMPLES):0;
    samples[slot]=capture(now_ms);
    if(samples_seen!=UINT32_MAX)++samples_seen;
}
bool battery_diagnostics_power(uint32_t now,BatteryPowerInfo *out)
{
    if(!out)return false;
    *out=(BatteryPowerInfo){0};
    if(!initialized || !supported || !samples_seen)return false;
    uint32_t index=samples_seen-1U;
    const BatterySample *s=samples+(NDVIDEO_BATTERY_TIMINGS?(index<FIRST_SAMPLES?index:
        FIRST_SAMPLES+(index-FIRST_SAMPLES)%RECENT_SAMPLES):0);
    out->mode=s->source;out->age_ms=now-s->now_ms;
    if(out->age_ms>3U*SAMPLE_PERIOD_MS || out->mode>6)return false;
    /* Native modes2..5 enable charging. Mode6 temporarily checks the battery
     * with charging switched off; show external-power/check, not charging. */
    out->charging=out->mode>=2 && out->mode<=5;
    out->checking=out->mode==6;
    return true;
}
#if NDVIDEO_BATTERY_TIMINGS
static void print_sample(FILE *file,unsigned slot)
{
    const BatterySample *s=samples+slot;
    fprintf(file,"battery_sample at_ms=%lu rtc=%lu irq=%02lx os_percent=%lu source=%lu "
        "raw=%lu trimmed_sum=%lu maximum=%lu minimum=%lu voltage_valid=%lu voltage_mv=%lu "
        "mode=%lu busy=%lu count=%lu bypass=%lu batch_stamp=%lu batch_duration=%lu\n",
        (unsigned long)s->now_ms,(unsigned long)s->rtc,(unsigned long)s->irq,
        (unsigned long)s->os_percent,(unsigned long)s->source,(unsigned long)s->raw,
        (unsigned long)s->trimmed_sum,(unsigned long)s->maximum,(unsigned long)s->minimum,
        (unsigned long)s->valid_voltage,(unsigned long)s->millivolts,(unsigned long)s->mode,(unsigned long)s->busy,
        (unsigned long)s->count,(unsigned long)s->bypass,(unsigned long)s->batch_stamp,
        (unsigned long)s->batch_duration);
}
void battery_diagnostics_debug(FILE *file)
{
    if(!file || !initialized)return;
    unsigned first=samples_seen<FIRST_SAMPLES?samples_seen:FIRST_SAMPLES;
    uint32_t tail=samples_seen>FIRST_SAMPLES?samples_seen-FIRST_SAMPLES:0;
    unsigned recent=tail<RECENT_SAMPLES?tail:RECENT_SAMPLES;
    unsigned oldest=tail>RECENT_SAMPLES?tail%RECENT_SAMPLES:0;
    fprintf(file,"battery_diagnostics version=1 supported=%u signature_failure=%lu samples=%lu kept=%u "
        "period_ms=%u cached_voltage_only=%u percent_estimate=0\n",supported,
        (unsigned long)signature_failure,(unsigned long)samples_seen,first+recent,SAMPLE_PERIOD_MS,!NDVIDEO_BATTERY_ACQUISITION);
    for(unsigned i=0;i<first;++i)print_sample(file,i);
    for(unsigned i=0;i<recent;++i)print_sample(file,FIRST_SAMPLES+(oldest+i)%RECENT_SAMPLES);
#if NDVIDEO_BATTERY_ACQUISITION || NDMEDIA_BATTERY_SERVICE
    fresh_debug(file);
#endif
}
#endif

#endif
