#include "battery_meter.h"
#include "battery_diagnostics.h"
#include "battery_power.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned native_calls,acquisitions;
static uint32_t last_acquired;
static bool adc_supported=true;
static bool power_known,power_present,power_host;
static unsigned power_calls,native_mode=1,metadata_delay;
static unsigned native_percent=100;
bool battery_usb_power(bool *external,bool *host)
{++power_calls;*external=power_present;*host=power_host;return power_known;}
static BatteryFreshReading reading;
void battery_native_init(BatteryNative *b){memset(b,0,sizeof(*b));}
bool battery_native_default(BatteryNative *b,BatteryNativeSnapshot *s,unsigned budget)
{
    assert(budget<=(b->complete?4096U:16384U));++native_calls;
    if(native_calls<=metadata_delay)return false;
    b->complete=true;
    *s=(BatteryNativeSnapshot){.capabilities=BATTERY_NATIVE_PERCENT|BATTERY_NATIVE_CHARGE_MODE|
        BATTERY_NATIVE_THRESHOLDS,.percent=native_percent,.charge_mode=native_mode,
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
    /* Missing voltage never substitutes a coarse OS percentage. */
    adc_supported=false;acquisitions=native_calls=0;reading=(BatteryFreshReading){0};
    battery_meter_init();battery_meter_tick(0,key,true);
    assert(battery_meter_info(&out) && !out.percent_valid && !out.voltage_valid && !out.estimated);
    assert(out.source!=BATTERY_METER_OS);
    assert(!acquisitions);
    const unsigned levels[]={100,75,50,25,10,2,0};
    for(unsigned i=0;i<7;++i) {
        native_percent=levels[i];battery_meter_init();battery_meter_tick(0,key,false);
        assert(battery_meter_info(&out) && !out.percent_valid && out.os_percent==levels[i]);
    }
    native_percent=100;
    /* Keep the native charge-mode cache unchanged while inserting/removing
     * the PC cable. Both directions must update without a screen transition. */
    native_mode=2;power_known=power_present=true;native_calls=power_calls=0;
    battery_meter_init();battery_meter_tick(0,key,false);
    assert(battery_meter_info(&out) && out.charging && out.external_power);
    uint32_t revision=battery_meter_revision();assert(revision && native_calls==1 && power_calls==1);
    power_present=false;battery_meter_tick(250,key,false);
    assert(battery_meter_info(&out) && !out.charging && !out.external_power && !out.checking);
    assert(battery_meter_revision()!=revision && native_calls==1 && power_calls==2);
    revision=battery_meter_revision();power_present=true;battery_meter_tick(500,key,false);
    assert(battery_meter_info(&out) && out.charging && out.external_power);
    assert(battery_meter_revision()!=revision && native_calls==1 && power_calls==3);
    revision=battery_meter_revision();battery_meter_tick(520,key,false);
    assert(battery_meter_revision()==revision && power_calls==3);
    /* Connection alone is external power, not proof the charge task enabled
     * charging. A cradle remains independent of the USB cable. */
    native_mode=1;battery_meter_init();battery_meter_tick(0,key,false);
    assert(battery_meter_info(&out) && out.external_power && !out.charging);
    revision=battery_meter_revision();power_present=false;power_host=true;
    battery_meter_tick(250,key,false);
    assert(battery_meter_info(&out) && out.usb_host && !out.external_power && !out.charging);
    assert(battery_meter_revision()!=revision);
    power_host=false;
    native_mode=4;power_present=false;battery_meter_init();battery_meter_tick(0,key,false);
    assert(battery_meter_info(&out) && out.charging && !out.external_power);
    /* Real estimate -> gated/unknown after OTG removal -> disconnected -> PC.
     * The OS mode/100% cache stays stale throughout, with no app-state change. */
    adc_supported=true;acquisitions=0;reading=(BatteryFreshReading){0};
    native_mode=2;power_known=true;power_present=false;power_host=true;
    battery_meter_init();battery_meter_tick(0,key,true);
    assert(battery_meter_info(&out) && out.percent_valid && out.estimated && out.usb_host && !out.charging);
    unsigned estimate=out.percent_tenths;assert(estimate<1000 && estimate>900);
    power_known=false;battery_meter_tick(250,key,false);
    assert(battery_meter_info(&out) && out.percent_tenths==estimate && !out.charging && !out.checking && !out.usb_host);
    power_known=true;power_host=false;battery_meter_tick(500,key,false);
    assert(battery_meter_info(&out) && out.percent_tenths==estimate && !out.charging && !out.external_power);
    power_present=true;battery_meter_tick(750,key,false);
    assert(battery_meter_info(&out) && out.percent_tenths==estimate && out.external_power && out.charging);
    power_known=false;battery_meter_tick(1000,key,false);
    assert(battery_meter_info(&out) && out.percent_tenths==estimate && !out.charging && !out.external_power);
    /* Cold launch with the PC cable already present: the very first fresh
     * acquisition exposes a percentage, independently of the OS100 cache. */
    for(unsigned mode=1;mode<=2;++mode) {
        acquisitions=0;reading=(BatteryFreshReading){0};
        power_known=power_present=true;power_host=false;native_mode=mode;
        battery_meter_init();battery_meter_tick(0,key,true);
        assert(battery_meter_info(&out) && out.percent_valid && out.voltage_valid &&
            out.external_power && out.estimated && out.source==BATTERY_METER_GENERAL &&
            out.percent_tenths>900 && out.percent_tenths<1000 && out.os_percent==100);
        assert(battery_meter_save());
        BatteryProfile charge_profile=read_profile(a);
        assert(charge_profile.last_run.valid_samples==1 && !charge_profile.last_run.discharge_samples);
    }
    /* Independent fresh voltage is ready on the first tick even if native
     * binding takes many foreground slices. Never train with missing metadata
     * or expose an invented OS0 value while that scan is still running. */
    for(unsigned plugged=0;plugged<2;++plugged) {
        acquisitions=native_calls=0;metadata_delay=64;reading=(BatteryFreshReading){0};
        native_mode=2;power_known=true;power_present=plugged!=0;power_host=false;
        battery_meter_init();battery_meter_tick(0,key,true);
        assert(battery_meter_info(&out) && out.percent_valid && out.voltage_valid &&
            out.os_percent==UINT32_MAX && out.source==BATTERY_METER_GENERAL);
        unsigned first=out.percent_tenths;
        assert(first>900 && first<1000 && acquisitions==1 && battery_meter_revision());
        for(unsigned t=20;t<=2000;t+=20)battery_meter_tick(t,key,true);
        assert(battery_meter_info(&out) && out.os_percent==100 && out.percent_tenths==first && acquisitions==1);
        assert(battery_meter_save());
        assert(!read_profile(a).last_run.discharge_samples);
    }
    metadata_delay=0;
    bool present=false,host=false;
    assert(battery_power_decode(0x202,0x01000010,0x01000010,0x003e0000,&present,&host) && present && !host);
    assert(battery_power_decode(0x202,0x01000010,0x01000010,0x00310000,&present,&host) && !present && !host);
    assert(battery_power_decode(0x202,0x01000010,0x01000010,0x008e0010,&present,&host) && !present && host);
    /* Released ID + residual host VBUS, then device role while the A-bus
     * request is still asserted, are neither an attached OTG cable nor input. */
    assert(battery_power_decode(0x202,0x01000010,0x01000010,0x00ae0010,&present,&host) && !present && !host);
    assert(battery_power_decode(0x202,0x01000010,0x01000010,0x003e0010,&present,&host) && !present && !host);
    assert(battery_power_decode(0x202,0x01000010,0x01000010,0x002e0020,&present,&host) && !present && !host);
    assert(battery_power_decode(0x202,0x01000010,0x01000010,0x003e0020,&present,&host) && present && !host);
    assert(battery_power_decode(0x202,0,0,0x003e0000,&present,&host) && present && !host);
    assert(!battery_power_decode(0x202,0,0,0,&present,&host));
    assert(!battery_power_decode(0x202,0x01000010,0x01000010,0x003f0000,&present,&host));
    assert(!battery_power_decode(0x202,UINT32_MAX,UINT32_MAX,UINT32_MAX,&present,&host));
    assert(battery_power_decode(0x101,0x01000040,0xfa05,0x0f00,&present,&host) && present && !host);
    assert(battery_power_decode(0x10000101,0x01000040,0xfa05,0x1100,&present,&host) && !present && !host);
    assert(battery_power_decode(0x101,0x01000040,0xfa05,0xe00,&present,&host) && !present && host);
    assert(!battery_power_decode(0x101,0,0xfa05,0x100,&present,&host));
    assert(!battery_power_decode(0x999,0x01000010,0,0x003e0000,&present,&host));
    puts("PASS quiet meter: foreground cadence, no playback file writes, atomic two-slot recovery, generation wrap, load identity and no coarse fallback");
    puts("PASS live power: cached OS charging survives neither USB removal nor reinsert; OTG output excluded, cradle preserved, bounded polls and visible revisions");
    puts("PASS no OS-percentage fallback: unknown/removed OTG and PC transitions preserve a genuine estimate without false charging or OS100 jumps");
    puts("PASS role handoff: released ID and self-sourced VBUS never become external charging; confirmed PC device role remains detectable");
    puts("PASS cold PC launch: fresh voltage percentage appears before unplugging, with no charge samples in discharge history");
    puts("PASS first-tick estimate: independent fresh ADC precedes delayed metadata discovery, no OS0 placeholder or unqualified learning");
    return 0;
}
