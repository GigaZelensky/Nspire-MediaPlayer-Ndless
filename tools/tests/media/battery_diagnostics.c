#include "battery_diagnostics.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "battery_adc_poll.h"
#if NDVIDEO_BATTERY_TIMINGS || NDMEDIA_BATTERY_SERVICE
static uint32_t handle[33],state[152],pointer=0x13fe0000U;
static unsigned reads,hash_reads,mask,fail_hash;
static unsigned charge_mode=1;
static bool cx2=true;
#if NDVIDEO_BATTERY_ACQUISITION || NDMEDIA_BATTERY_SERVICE
static bool mapping_ok=true,mutate_cache,restore_failure;
static unsigned adc_calls;
static uint32_t pmu_handle[33],pmu_state[8];
void adc_poll(AdcPollBus bus,unsigned channel,unsigned reference,AdcPollResult *out)
{
    (void)bus;assert(mask==0xc0 && channel==1 && reference==1);
    assert(!battery_diagnostics_acquisition_due(1000000) && !battery_diagnostics_acquire(1000000));
    ++adc_calls;*out=(AdcPollResult){.raw=947,.conversions=10,.restored=!restore_failure,.touched=true,
        .result=restore_failure?ADC_POLL_RESTORE:ADC_POLL_OK};
    if(mutate_cache)++state[156/4];
}
#endif
bool nspire_is_cx2(void) { return cx2; }
unsigned native_critical_enter(void) { unsigned saved=mask;mask=0xc0;return saved; }
void native_critical_leave(unsigned saved) { assert(mask==0xc0);mask=saved; }
static uint32_t test_read32(uint32_t at)
{
    assert(mask==0xc0 && !(at&3));++reads;
    if(at==0x90090000U)return 123456;
    if(at==0x113ebd68U+48)return 0;
    if(at==0x10b189c0U)return 100;
    if(at==0x113ebf10U+40)return charge_mode;
    if(at==0x113c10d0U)return pointer;
#if NDVIDEO_BATTERY_ACQUISITION || NDMEDIA_BATTERY_SERVICE
    if(at==0x113c3e20U)return 0x13fc0000U;
    if(at>=0x13fc0000U && at<0x13fc0000U+sizeof(pmu_handle))return pmu_handle[(at-0x13fc0000U)/4];
    if(at>=0x13fb0000U && at<0x13fb0000U+sizeof(pmu_state))return pmu_state[(at-0x13fb0000U)/4];
#endif
    if(at>=0x13fe0000U && at<0x13fe0000U+sizeof(handle))return handle[(at-0x13fe0000U)/4];
    if(at>=0x13fd0000U && at<0x13fd0000U+sizeof(state))return state[(at-0x13fd0000U)/4];
    assert(!"Unexpected battery memory read");return 0;
}
static uint32_t test_read16(uint32_t at) { return (test_read32(at&~3U)>>(8*(at&3U)))&65535U; }
static uint32_t test_read8(uint32_t at) { return (test_read32(at&~3U)>>(8*(at&3U)))&255U; }
static uint32_t test_fingerprint(uint32_t at,unsigned bytes)
{
    static const uint32_t known[][3]={
        {0x10003974U,44,0xa6b5d862U},{0x10004600U,164,0xc888636fU},
        {0x100048c4U,388,0x0bf56b99U},{0x10102c18U,128,0x854af231U},
        {0x10103660U,44,0x54141d1aU},{0x10b189c0U,28,0x43d38119U},
        {0x10003c30U,852,0xe0fdecb9U},{0x100041e8U,196,0xe612d90fU},
        {0x10003b20U,176,0xc05e4212U},{0x10003becU,68,0x8a4d30f3U},
        {0x10003f84U,116,0x302061c9U},{0x10001df8U,44,0x16b617e8U},
        {0x10001474U,64,0x7629d87cU},{0x10001528U,64,0xea844c2aU},
        {0x10001d3cU,80,0x9369a2f9U},{0x10001730U,1528,0x91870bbfU},
    };
    ++hash_reads;
    for(unsigned i=0;i<sizeof(known)/sizeof(*known);++i)
        if(at==known[i][0]) {assert(bytes==known[i][1]);return fail_hash==i+1?0:known[i][2];}
    assert(!"Unexpected firmware read");return 0;
}
#endif
/* PRODUCTION_BATTERY_DIAGNOSTICS */
int main(void)
{
#if NDMEDIA_BATTERY_SERVICE && !NDVIDEO_BATTERY_TIMINGS
    handle[68/4]=0x494f4452U;handle[100/4]=4;handle[112/4]=0x100048c4U;
    handle[76/4]=0x13fd0000U;handle[12/4]=1;
    state[8/4]=1;state[16/4]=1750;state[48/4]=1;state[84/4]=2932;state[88/4]=3901;
    state[604/4]=1;state[156/4]=947;
    pmu_handle[68/4]=0x494f4452U;pmu_handle[100/4]=15;pmu_handle[112/4]=0x10001730U;
    pmu_handle[76/4]=0x13fb0000U;pmu_state[1]=0x14;
    battery_diagnostics_init();mask=0x80;
    assert(sizeof(fresh_samples)/sizeof(*fresh_samples)==1);
    for(unsigned i=0;i<40;++i) {
        battery_diagnostics_tick(i*5000U);
        assert(battery_diagnostics_acquire(i*5000U));
    }
    BatteryFreshReading r;
    assert(battery_diagnostics_latest(&r) && r.valid && r.sequence==40 && r.now_ms==195000 && r.millivolts==4151);
    FILE *f=tmpfile();assert(f);battery_diagnostics_debug(f);assert(ftell(f)==0);fclose(f);
    assert(mask==0x80 && fresh_seen==40 && samples_seen==40 && adc_calls==40);
    puts("PASS quiet battery service: valid current sample, one retained acquisition, unchanged hardware state and zero diagnostic output");
#elif !NDVIDEO_BATTERY_TIMINGS
    battery_diagnostics_init();battery_diagnostics_tick(100);
    FILE *f=tmpfile();assert(f);battery_diagnostics_debug(f);assert(ftell(f)==0);fclose(f);
    puts("PASS production battery diagnostics disabled: no reads, samples or output");
#else
    cx2=false;battery_diagnostics_init();battery_diagnostics_tick(0);
    assert(!reads && !hash_reads && !supported);
    cx2=true;fail_hash=7;battery_diagnostics_init();battery_diagnostics_tick(0);
    assert(!reads && signature_failure==7 && !supported);
    fail_hash=0;
    handle[68/4]=0x494f4452U;handle[100/4]=4;handle[112/4]=0x100048c4U;
    handle[76/4]=0x13fd0000U;
    state[8/4]=1;state[16/4]=1750;state[48/4]=1;state[84/4]=2932;state[88/4]=3901;
    state[604/4]=1;state[272/4]=0xffffU|(7U<<16);
    state[156/4]=950;state[160/4]=7603;state[164/4]=958;state[168/4]=948;
    uint32_t before_state[152],before_handle[33];
    memcpy(before_state,state,sizeof(state));memcpy(before_handle,handle,sizeof(handle));
    battery_diagnostics_init();battery_diagnostics_tick(0);
    assert(supported && samples_seen==1 && !mask && samples[0].valid_voltage && samples[0].millivolts==4164);
    assert(samples[0].batch_stamp==65535 && samples[0].batch_duration==7 && samples[0].trimmed_sum==7603);
    assert(!memcmp(before_state,state,sizeof(state)) && !memcmp(before_handle,handle,sizeof(handle)));
    unsigned previous=reads;battery_diagnostics_tick(4999);assert(reads==previous);
    state[156/4]=947;state[160/4]=7583;state[272/4]=1|(9U<<16);
    battery_diagnostics_tick(5000);assert(samples_seen==2 && samples[1].millivolts==4151);
    assert(samples[1].batch_stamp==1 && samples[1].batch_duration==9 && samples[1].os_percent==100);
    /* Bad pointer, handle, channel, sentinel and calibration never yield a voltage. */
    pointer=3;assert(!capture(0).valid_voltage);pointer=0x13fe0000U;
    handle[112/4]=0;assert(!capture(0).valid_voltage);handle[112/4]=0x100048c4U;
    state[48/4]=8;state[84/4]=0;assert(!capture(0).valid_voltage);
    state[48/4]=1;state[84/4]=2932;
    state[156/4]=0x6969;assert(!capture(0).valid_voltage);state[156/4]=947;
    state[88/4]=2932;assert(!capture(0).valid_voltage);state[88/4]=3901;
    /* Wrap-safe sampling interval, including a wrapping native marker. */
    battery_diagnostics_init();battery_diagnostics_tick(UINT32_MAX-1000U);previous=reads;
    battery_diagnostics_tick(2000);assert(reads==previous);
    battery_diagnostics_tick(4000);assert(samples_seen==2);
    battery_diagnostics_init();
    for(unsigned i=0;i<40;++i)battery_diagnostics_tick(i*5000U);
    FILE *f=tmpfile();assert(f);battery_diagnostics_debug(f);
    long length=ftell(f);assert(length>0 && length<16384);
    char *report=calloc((size_t)length+1,1);assert(report);rewind(f);
    assert(fread(report,1,(size_t)length,f)==(size_t)length);fclose(f);
    unsigned rows=0;for(char *p=report;(p=strstr(p,"battery_sample "))!=NULL;++p)++rows;
    assert(rows==32 && strstr(report,"samples=40 kept=32") && strstr(report,"percent_estimate=0") &&
        strstr(report,"at_ms=35000 ") && !strstr(report,"at_ms=40000 ") &&
        strstr(report,"at_ms=80000 ") && strstr(report,"at_ms=195000 "));
    free(report);assert(!mask);
    BatteryPowerInfo power;
    assert(battery_diagnostics_power(195000,&power) && !power.charging && !power.checking);
    charge_mode=2;battery_diagnostics_tick(200000);
    assert(battery_diagnostics_power(200000,&power) && power.charging && !power.checking);
    charge_mode=6;battery_diagnostics_tick(205000);
    assert(battery_diagnostics_power(205000,&power) && !power.charging && power.checking);
    assert(!battery_diagnostics_power(220001,&power));
    charge_mode=7;battery_diagnostics_tick(225000);assert(!battery_diagnostics_power(225000,&power));
    charge_mode=1;
    puts("PASS passive battery observer: hardware calibration, firmware/model/pointer guards, unchanged RAM, wrap-safe cadence and bounded report");
#if NDVIDEO_BATTERY_ACQUISITION || NDMEDIA_BATTERY_SERVICE
    pmu_handle[68/4]=0x494f4452U;pmu_handle[100/4]=15;pmu_handle[112/4]=0x10001730U;
    pmu_handle[76/4]=0x13fb0000U;pmu_state[1]=0x14;handle[12/4]=1;
    battery_diagnostics_init();mask=0x80;
    BatteryFreshReading latest;
    assert(!battery_diagnostics_latest(&latest));
    assert(battery_diagnostics_acquisition_due(0) && battery_diagnostics_acquire(0));
    assert(mask==0x80 && fresh_seen==1 && adc_calls==1 && !fresh_samples[0].guard &&
        fresh_samples[0].fresh_mv==4151 && fresh_samples[0].cache_unchanged);
    assert(battery_diagnostics_latest(&latest) && latest.valid && latest.sequence==1 &&
        latest.millivolts==4151 && latest.charge_mode==1 && latest.os_percent==100);
    latest.millivolts=1;
    assert(battery_diagnostics_latest(&latest) && latest.millivolts==4151);
    assert(!battery_diagnostics_acquisition_due(4999) && !battery_diagnostics_acquire(4999));
    mapping_ok=false;assert(battery_diagnostics_acquire(5000));assert(fresh_samples[1].guard==2 && adc_calls==1);mapping_ok=true;
    assert(battery_diagnostics_latest(&latest) && !latest.valid && latest.sequence==2 && latest.guard==2);
    state[4/4]=1;battery_diagnostics_acquire(10000);assert(fresh_samples[2].guard==3 && adc_calls==1);state[4/4]=0;
    pmu_state[1]=3;battery_diagnostics_acquire(15000);assert(fresh_samples[3].guard==4 && adc_calls==1);pmu_state[1]=0x14;
    mask=0;battery_diagnostics_acquire(20000);assert(fresh_samples[4].guard==6 && adc_calls==1 && !mask);mask=0x80;
    mutate_cache=true;battery_diagnostics_acquire(25000);assert(fresh_samples[5].guard==7 && !fresh_samples[5].fresh_mv);
    assert(!battery_diagnostics_acquisition_due(1000000));mutate_cache=false;
    battery_diagnostics_init();restore_failure=true;battery_diagnostics_acquire(0);
    assert(!fresh_samples[0].fresh_mv && !battery_diagnostics_acquisition_due(1000000));restore_failure=false;
    battery_diagnostics_init();
    for(unsigned i=0;i<40;++i)battery_diagnostics_acquire(i*5000U);
    assert(battery_diagnostics_latest(&latest) && latest.valid && latest.sequence==40 && latest.now_ms==195000);
    f=tmpfile();assert(f);battery_diagnostics_debug(f);length=ftell(f);assert(length>0 && length<32768);
    report=calloc((size_t)length+1,1);assert(report);rewind(f);assert(fread(report,1,(size_t)length,f)==(size_t)length);fclose(f);
    rows=0;for(char *p=report;(p=strstr(p,"battery_acquire "))!=NULL;++p)++rows;
    assert(rows==16 && strstr(report,"seen=40 kept=16") && strstr(report,"at_ms=35000 ") &&
        !strstr(report,"at_ms=40000 ") && strstr(report,"at_ms=160000 ") && strstr(report,"at_ms=195000 "));
    free(report);mask=0;
    puts("PASS explicit battery acquisition: cadence/reentrancy, foreground/mapping/native-idle/PMU guards, calibration/cache integrity and bounded retention");
#endif
#endif
    return 0;
}
