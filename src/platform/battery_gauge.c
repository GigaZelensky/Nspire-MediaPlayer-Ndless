#include "battery_gauge.h"
#if NDMEDIA_BATTERY_GAUGE_TEST
#include <string.h>
enum {FRESH_MS=15000,SMOOTH_MS=30000};
static const BatteryGaugeProfile *profile;
static BatteryGaugeInfo state;
static uint32_t last_sequence,last_ms,filtered_q8;
static bool have_reading,have_filter,current_reference;

static bool profile_valid(const BatteryGaugeProfile *p)
{
    if(!p || !p->points || p->count<2 || p->count>512 || p->display_offset_mv>100 ||
        !p->cpu_mhz || p->volume>100 || !p->rate || p->channels<1 || p->channels>2)return false;
    for(unsigned i=0;i<p->count;++i) {
        BatteryGaugePoint q=p->points[i];
        if(q.millivolts<2500 || q.millivolts>4500 || q.percent_tenths>1000)return false;
        if(i && (q.millivolts<=p->points[i-1].millivolts || q.percent_tenths<p->points[i-1].percent_tenths))return false;
    }
    return true;
}
bool battery_gauge_init(const BatteryGaugeProfile *p)
{
    memset(&state,0,sizeof(state));last_sequence=last_ms=filtered_q8=0;
    have_reading=have_filter=current_reference=false;
    profile=profile_valid(p)?p:NULL;return profile!=NULL;
}
static bool reference_context(const BatteryCalibrationContext *c)
{
    if(!profile || !c || c->cpu_mhz!=profile->cpu_mhz || c->volume!=profile->volume || c->muted ||
       c->codec!=profile->codec || c->rate!=profile->rate || c->channels!=profile->channels)return false;
    unsigned flags=c->flags;
    if(flags!=(BATTERY_CAL_PLAYING|BATTERY_CAL_USB_SOURCE) &&
        flags!=(BATTERY_CAL_PLAYING|BATTERY_CAL_USB_SOURCE|BATTERY_CAL_SCREEN_OFF))return false;
    return (flags&BATTERY_CAL_SCREEN_OFF) || c->brightness==profile->on_brightness;
}
static unsigned lookup(unsigned mv)
{
    if(mv<=profile->points[0].millivolts)return profile->points[0].percent_tenths;
    for(unsigned i=1;i<profile->count;++i) {
        BatteryGaugePoint a=profile->points[i-1],b=profile->points[i];
        if(mv<=b.millivolts) {
            unsigned span=b.millivolts-a.millivolts;
            return a.percent_tenths+((mv-a.millivolts)*(b.percent_tenths-a.percent_tenths)+span/2)/span;
        }
    }
    return profile->points[profile->count-1].percent_tenths;
}
void battery_gauge_update(uint32_t now,const BatteryFreshReading *r,const BatteryCalibrationContext *context)
{
    current_reference=reference_context(context);
    if(!current_reference)have_filter=false;
    if(!r || !r->sequence || r->sequence==last_sequence)return;
    last_sequence=r->sequence;
    uint32_t dt=r->now_ms-last_ms;
    have_reading=r->valid && !r->guard && !r->result && r->restored && r->cache_unchanged &&
        r->millivolts>=2500 && r->millivolts<=4500 && (uint32_t)(now-r->now_ms)<=FRESH_MS;
    state=(BatteryGaugeInfo){.millivolts=r->millivolts,
        .charging=r->charge_mode>=2 && r->charge_mode<=5,.checking=r->charge_mode==6};
    last_ms=r->now_ms;
    if(!have_reading){have_filter=false;return;}
    if(!current_reference || r->charge_mode!=1){have_filter=false;return;}
    bool adjusted=!(context->flags&BATTERY_CAL_SCREEN_OFF);
    uint32_t target=(r->millivolts+(adjusted?profile->display_offset_mv:0))*256U;
    if(!have_filter || dt>FRESH_MS)filtered_q8=target;
    else if(dt)filtered_q8=(uint32_t)(((uint64_t)filtered_q8*SMOOTH_MS+(uint64_t)target*dt)/(SMOOTH_MS+dt));
    have_filter=true;
    state.reference_mv=(filtered_q8+128U)/256U;
    state.percent_tenths=lookup(state.reference_mv);
    state.adjusted=adjusted;
}
bool battery_gauge_info(uint32_t now,BatteryGaugeInfo *out)
{
    if(!out)return false;
    *out=state;out->age_ms=now-last_ms;
    out->voltage_valid=have_reading && out->age_ms<=FRESH_MS;
    out->reference_load=current_reference;
    out->percent_valid=out->voltage_valid && current_reference && have_filter;
    return out->voltage_valid;
}
void battery_gauge_debug(FILE *f)
{
    if(!f)return;
    fprintf(f,"battery_gauge experimental=1 profile_valid=%u reference_load=%u reading=%u filtered=%u "
        "last_ms=%lu raw_mv=%lu reference_mv=%lu percent_tenths=%lu adjusted=%u charging=%u\n",
        profile!=NULL,current_reference,have_reading,have_filter,(unsigned long)last_ms,
        (unsigned long)state.millivolts,(unsigned long)state.reference_mv,(unsigned long)state.percent_tenths,
        state.adjusted,state.charging);
    if(profile && profile->description)fprintf(f,"battery_gauge_profile %s\n",profile->description);
}
#endif
