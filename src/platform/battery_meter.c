#include "battery_meter.h"
#if NDMEDIA_BATTERY_METER
#include "battery_diagnostics.h"
#include "battery_power.h"
#include "../player/player_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BatteryModel model;
static BatteryNative native;
static BatteryMeterInfo visible;
static uint32_t last_poll;
static bool visible_valid,polled;
static bool power_polled,power_known,power_present,power_host,appearance_valid;
static uint32_t last_power_poll,revision,appearance;
static int saved_slot=-1;
/* Settings only: no CSV, no report, no per-frame/per-sample disk writes. */
static const char *const names[]={"nMedia-battery-a.dat.tns","nMedia-battery-b.dat.tns"};
void battery_meter_init(void)
{
    battery_model_init(&model);battery_native_init(&native);
    visible=(BatteryMeterInfo){0};visible_valid=polled=false;last_poll=0;saved_slot=-1;
    power_polled=power_known=power_present=power_host=appearance_valid=false;
    last_power_poll=revision=appearance=0;
}
uint32_t battery_meter_load_key(bool video,bool usb,uint32_t codec,uint32_t mhz,
                               uint32_t brightness,bool off,uint32_t volume,
                               uint32_t rate,uint32_t channels,uint32_t topology)
{
    /* Different loads never train one shared drain curve. A screen change
     * starts a new settled segment; no unmeasured display-sag constant. */
    uint32_t h=2166136261U,fields[]={video,usb,codec,mhz,off?0:brightness+1,
        volume/10,rate,channels,topology};
    for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);++i)h=(h^fields[i])*16777619U;
    return h?h:1;
}
void battery_meter_tick(uint32_t now,uint32_t key,bool acquire)
{
    battery_model_context(&model,now,key);
    bool power_changed=false;
    if(!power_polled || now-last_power_poll>=250U) {
        bool present=false,host=false,known=battery_usb_power(&present,&host);
        power_changed=known!=power_known || (known && (present!=power_present || host!=power_host));
        power_known=known;power_present=present;power_host=host;last_power_poll=now;power_polled=true;
        battery_model_power(&model,now,known,present,host);
    }
    bool due=!polled || now-last_poll>=1000;
    if(!native.complete || due) {
        BatteryNativeSnapshot snapshot;
        if(battery_native_default(&native,&snapshot,native.complete?4096:16384))battery_model_native(&model,now,&snapshot);
    }
    if(acquire && battery_diagnostics_acquisition_due(now))battery_diagnostics_acquire(now);
    BatteryFreshReading r;
    if(battery_diagnostics_latest(&r)) {
        if(r.valid && now-r.now_ms<=15000)
            battery_model_sample(&model,r.now_ms,r.rtc,r.sequence,r.millivolts,r.charge_mode,key);
        else {model.anchor_valid=false;model.load_key=0;}
    }
    if(due || power_changed) {
        visible_valid=battery_model_info(&model,now,&visible);
        if(due){last_poll=now;polled=true;}
        uint32_t state=(visible_valid?1U:0U) | (visible.percent_valid?2U:0U) |
            (visible.charging?4U:0U) | (visible.checking?8U:0U) | (visible.external_power?16U:0U) |
            (visible.usb_host?32U:0U);
        if(visible.percent_valid)state|=((visible.percent_tenths+5U)/10U)<<8 |
            ((14U*visible.percent_tenths+500U)/1000U)<<16;
        if(!appearance_valid || state!=appearance) {
            appearance=state;appearance_valid=true;if(!++revision)++revision;
        }
    }
}
bool battery_meter_info(BatteryMeterInfo *out)
{if(out)*out=visible;return out && visible_valid;}
uint32_t battery_meter_revision(void){return revision;}
void battery_meter_debug(FILE *file)
{
    if(file)fprintf(file,"battery_meter_binding initialized=%u complete=%u words=%lu caps=%08lx errors=%08lx native_valid=%u visible=%u fresh=%u percent_valid=%u percent_tenths=%u\n",
        native.initialized,native.complete,(unsigned long)native.scanned_words,(unsigned long)native.capabilities,
        (unsigned long)native.errors,model.native_valid,visible_valid,model.fresh_valid,
        visible.percent_valid,(unsigned)visible.percent_tenths);
}
void battery_meter_reset(void)
{battery_model_reset(&model);polled=false;}
void battery_meter_load(void)
{
    uint8_t *data=malloc(BATTERY_PROFILE_BYTES);
    BatteryProfile *profile=malloc(sizeof(*profile));
    if(!data || !profile){free(data);free(profile);return;}
    char path[512];
    for(unsigned i=0;i<2;++i)if(player_data_path(names[i],path,sizeof(path))) {
        FILE *file=fopen(path,"rb");if(!file)continue;
        size_t n=fread(data,1,BATTERY_PROFILE_BYTES,file);bool end=feof(file) && !ferror(file);
        fclose(file);
        if(end && battery_profile_decode(profile,data,n) &&
           (saved_slot<0 || battery_profile_newer(profile->generation,model.profile.generation))) {
            model.profile=*profile;saved_slot=(int)i;
        }
    }
    free(data);free(profile);
}
bool battery_meter_save(void)
{
    if(model.run.valid_samples) {
        model.profile.last_run=model.run;model.profile.dirty=true;
    }
    if(!model.profile.dirty)return true;
    uint8_t data[BATTERY_PROFILE_BYTES];char path[512];
    unsigned slot=saved_slot==0?1:0;
    uint32_t generation=model.profile.generation+1;
    size_t n=battery_profile_encode(&model.profile,data,sizeof(data),generation);
    if(!n || !player_data_prepare() || !player_data_path(names[slot],path,sizeof(path)))return false;
    FILE *file=fopen(path,"wb");if(!file)return false;
    bool ok=fwrite(data,1,n,file)==n;
    if(fclose(file)!=0)ok=false;
    /* Only advance after complete write/close. The previously valid slot is
     * untouched even if power fails while writing the other slot. */
    if(ok){saved_slot=(int)slot;model.profile.generation=generation;model.profile.dirty=false;}
    return ok;
}
#endif
