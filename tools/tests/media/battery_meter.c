#include "battery_meter.h"
#include "battery_diagnostics.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned native_calls,acquisitions;
static uint32_t last_acquired;
static bool adc_supported=true;
static BatteryFreshReading reading;
void battery_native_init(BatteryNative *b){memset(b,0,sizeof(*b));}
bool battery_native_default(BatteryNative *b,BatteryNativeSnapshot *s,unsigned budget)
{
    assert(budget<=(b->complete?4096U:16384U));++native_calls;b->complete=true;
    *s=(BatteryNativeSnapshot){.capabilities=BATTERY_NATIVE_PERCENT|BATTERY_NATIVE_CHARGE_MODE|
        BATTERY_NATIVE_THRESHOLDS,.percent=100,.charge_mode=1,
        .levels={100,75,50,25,10,2,0},
        .thresholds={{3900,0},{3800,4000},{3740,3900},{3650,3840},{3500,3800},{3400,3750},{0,3600}}};
    return true;
}
bool battery_diagnostics_acquisition_due(uint32_t n)
{return adc_supported && (!acquisitions || n-last_acquired>=5000);}
bool battery_diagnostics_acquire(uint32_t n)
{
    assert(battery_diagnostics_acquisition_due(n));++acquisitions;last_acquired=n;
    reading=(BatteryFreshReading){.sequence=acquisitions,.now_ms=n,.rtc=20000U*86400U+n/1000,
        .millivolts=4000-n/10000,.charge_mode=1,.valid=true};return true;
}
bool battery_diagnostics_latest(BatteryFreshReading *r){*r=reading;return acquisitions!=0;}
bool player_data_prepare(void){return true;}
bool player_data_path(const char *name,char *path,size_t n)
{return snprintf(path,n,"%s",name)>0;}
static bool exists(const char *name)
{FILE *f=fopen(name,"rb");if(!f)return false;fclose(f);return true;}
static BatteryProfile read_profile(const char *name)
{
    uint8_t data[BATTERY_PROFILE_BYTES];BatteryProfile p;
    FILE *f=fopen(name,"rb");assert(f);size_t n=fread(data,1,sizeof(data),f);fclose(f);
    assert(battery_profile_decode(&p,data,n));return p;
}
static const char *const a="nMedia-battery-a.dat.tns",*const z="nMedia-battery-b.dat.tns";
int main(void)
{
    battery_meter_init();battery_meter_load();
    BatteryMeterInfo out;
    assert(!battery_meter_info(&out));
    uint32_t key=battery_meter_load_key(false,true,3,396,0,false,80,44100,2,55);
    assert(key && key!=battery_meter_load_key(false,true,3,396,0,true,80,44100,2,55));
    assert(key!=battery_meter_load_key(false,true,3,396,0,false,80,44100,2,56));
    for(unsigned n=0;n<200000;n+=20)battery_meter_tick(n,key,true);
    assert(native_calls==200 && acquisitions==40 && battery_meter_info(&out));
    assert(out.voltage_valid && out.estimated);
    assert(out.source==BATTERY_METER_GENERAL && out.os_percent==100);
    assert(!exists(a) && !exists(z)); /* zero routine files while playing */
    battery_meter_tick(200000,0,false);assert(acquisitions==40);
    battery_meter_reset();assert(battery_meter_save() && exists(a) && !exists(z));
    assert(read_profile(a).generation==1);
    BatteryProfile saved=read_profile(a);
    assert(saved.last_run.valid_samples==40 && saved.last_run.discharge_samples==40 && saved.last_run.playback_samples==40);
    assert(saved.last_run.max_mv==4000 && saved.last_run.min_mv==3981 && saved.last_run.playback_max_mv==4000);
    battery_meter_reset();assert(battery_meter_save() && read_profile(z).generation==2);
    /* Interrupted overwrite: the old slot remains usable on next launch. */
    FILE *f=fopen(z,"wb");assert(f);assert(fwrite("broken",1,6,f)==6);fclose(f);
    battery_meter_init();battery_meter_load();battery_meter_reset();
    assert(battery_meter_save() && read_profile(z).generation==2);
    /* Valid wraparound generations select zero as newer than UINT32_MAX. */
    uint8_t data[BATTERY_PROFILE_BYTES];BatteryProfile p={0};
    size_t size=battery_profile_encode(&p,data,sizeof(data),UINT32_MAX);
    f=fopen(a,"wb");assert(f);assert(fwrite(data,1,size,f)==size);fclose(f);
    size=battery_profile_encode(&p,data,sizeof(data),0);
    f=fopen(z,"wb");assert(f);assert(fwrite(data,1,size,f)==size);fclose(f);
    battery_meter_init();battery_meter_load();battery_meter_reset();assert(battery_meter_save());
    assert(read_profile(a).generation==1);
    /* Classic/unknown ADC hardware still has the read-only OS fallback. */
    adc_supported=false;acquisitions=native_calls=0;reading=(BatteryFreshReading){0};
    battery_meter_init();battery_meter_tick(0,key,true);
    assert(battery_meter_info(&out) && out.percent_tenths==1000 && !out.voltage_valid && !out.estimated);
    assert(out.source==BATTERY_METER_OS);
    assert(!acquisitions);
    puts("PASS quiet meter: foreground cadence, no playback file writes, atomic two-slot recovery, generation wrap, load identity and native-only fallback");
    return 0;
}
