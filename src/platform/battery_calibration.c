#include "battery_calibration.h"
#if NDMEDIA_BATTERY_CALIBRATION
#include <string.h>

/* At most 4096 compact observations, normally one every 30 s (34 h).
 * Fresh acquisition still runs every 5 s for the low-voltage stop. The ADC
 * path has already released IRQ exclusion before any of these operations. */
enum { CAPACITY=4096, RECORD_MS=NDMEDIA_BATTERY_LOAD_TEST?5000:30000,
       LOW_MV=3500, LOW_HOLD_MS=30000,
       MAX_AGE_MS=NDMEDIA_BATTERY_LOAD_TEST?180000:24*60*60*1000, MISSING_MS=120000 };
typedef struct {
    BatteryFreshReading reading;
    BatteryCalibrationContext context;
    uint32_t elapsed_ms,playing_ms,stable_ms;
} CalibrationRow;
static CalibrationRow rows[CAPACITY];
static uint32_t count,start_ms,last_tick_ms,playing_ms,context_at,last_sequence;
static uint32_t last_record_ms,last_valid_ms,low_at,low_last,attempts,valid_attempts,rejected;
static BatteryCalibrationContext current;
static bool started,have_context,have_valid,low_pending,have_record;
static const char *stop_reason;

static bool same_context(const BatteryCalibrationContext *a,const BatteryCalibrationContext *b)
{
    return a->flags==b->flags && a->cpu_mhz==b->cpu_mhz && a->brightness==b->brightness &&
        a->volume==b->volume && a->muted==b->muted && a->codec==b->codec &&
        a->rate==b->rate && a->channels==b->channels;
}
void battery_calibration_init(void)
{
    count=start_ms=last_tick_ms=playing_ms=context_at=last_sequence=0;
    last_record_ms=last_valid_ms=low_at=low_last=attempts=valid_attempts=rejected=0;
    started=have_context=have_valid=low_pending=have_record=false;stop_reason=NULL;
    memset(&current,0,sizeof(current));
}
void battery_calibration_start(uint32_t now)
{
    if(started)return;
    started=true;start_ms=last_tick_ms=context_at=last_valid_ms=now;
}
void battery_calibration_abort(void)
{
    if(started && !stop_reason)stop_reason="playback_stopped";
}
static void retain(uint32_t now,const BatteryFreshReading *r)
{
    if(count==CAPACITY){stop_reason="capacity";return;}
    rows[count++]=(CalibrationRow){.reading=*r,.context=current,
        .elapsed_ms=now-start_ms,.playing_ms=playing_ms,.stable_ms=now-context_at};
    last_record_ms=now;have_record=true;
}
void battery_calibration_tick(uint32_t now,const BatteryFreshReading *r,const BatteryCalibrationContext *context)
{
    if(!started || stop_reason || !context)return;
    uint32_t dt=now-last_tick_ms;last_tick_ms=now;
    if(have_context && (current.flags&BATTERY_CAL_PLAYING) && !(current.flags&BATTERY_CAL_PAUSED))
        /* A blocked/recovery/native-menu interval is not verified playback. */
        playing_ms+=dt<=1000U?dt:0;
    if(!have_context || !same_context(&current,context)) {
        current=*context;context_at=now;have_context=true;
    }
    if((uint32_t)(now-start_ms)>=MAX_AGE_MS)
        stop_reason=NDMEDIA_BATTERY_LOAD_TEST?"load_complete":"time_limit";
    if(r && r->sequence!=last_sequence && (uint32_t)(now-r->now_ms)<=1000U) {
        last_sequence=r->sequence;++attempts;
        bool valid=r->valid && r->millivolts>=2500 && r->millivolts<=4500;
        if(valid){++valid_attempts;last_valid_ms=now;have_valid=true;}else{++rejected;low_pending=false;}
        /* Fresh voltage during charging is not an unloaded/discharge point. */
        if(valid && r->charge_mode>=2 && r->charge_mode<=6)stop_reason="charging";
        if(valid && r->charge_mode!=1) {
            low_pending=false;
            if(!stop_reason)stop_reason="charge_state";
        } else if(valid && r->millivolts<=LOW_MV) {
            if(!low_pending || (uint32_t)(now-low_last)>10000U){low_at=now;low_pending=true;}
            low_last=now;
            if((uint32_t)(now-low_at)>=LOW_HOLD_MS)stop_reason="low_voltage";
        } else if(valid)low_pending=false;
        if(!have_record || (uint32_t)(now-last_record_ms)>=RECORD_MS || !valid || stop_reason)
            retain(now,r);
    }
    if(!stop_reason && (uint32_t)(now-last_valid_ms)>=MISSING_MS)
        stop_reason=have_valid?"samples_lost":"no_fresh_samples";
}
bool battery_calibration_active(void){return started && !stop_reason;}
bool battery_calibration_stopped(void){return started && stop_reason;}
bool battery_calibration_has_samples(void){return count!=0;}
const char *battery_calibration_reason(void){return stop_reason?stop_reason:"manual_exit";}
int battery_calibration_screen_target(uint32_t now)
{
    if(!NDMEDIA_BATTERY_LOAD_TEST || !started || stop_reason)return -1;
    uint32_t elapsed=now-start_ms;
    return elapsed>=60000U && elapsed<120000U?1:0;
}
bool battery_calibration_report(FILE *file)
{
    if(!file || !started)return false;
    fprintf(file,"# nMedia battery %s v1\n# build=%s %s\n"
        "# end_reason=%s\n# low_endpoint_mv=%u\n# low_hold_ms=%u\n"
        "# started_at_ms=%lu\n# attempts=%lu\n# valid_attempts=%lu\n# rejected=%lu\n# rows=%lu\n"
        "# full_charge=requires_user_confirmation\n"
        "# scope=usable playback discharge profile; endpoint is a reserve, not chemical zero charge\n"
        "# storage=RAM only until normal app cleanup; one overwritten report\n",
        NDMEDIA_BATTERY_LOAD_TEST?"load comparison":"calibration",
        __DATE__,__TIME__,battery_calibration_reason(),LOW_MV,LOW_HOLD_MS,
        (unsigned long)start_ms,(unsigned long)attempts,(unsigned long)valid_attempts,
        (unsigned long)rejected,(unsigned long)count);
    fputs("elapsed_ms,rtc,mv,cached_mv,raw,os_percent,charge_mode,guard,result,valid,restored,cache_unchanged,flags,cpu_mhz,brightness,volume,muted,codec,rate,channels,stable_ms,playing_ms\n",file);
    for(uint32_t i=0;i<count;++i) {
        const CalibrationRow *row=rows+i;const BatteryFreshReading *r=&row->reading;
        const BatteryCalibrationContext *c=&row->context;
        fprintf(file,"%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
            (unsigned long)row->elapsed_ms,(unsigned long)r->rtc,(unsigned long)r->millivolts,
            (unsigned long)r->cached_mv,(unsigned long)r->raw,(unsigned long)r->os_percent,
            (unsigned long)r->charge_mode,(unsigned long)r->guard,(unsigned long)r->result,r->valid,
            (unsigned long)r->restored,(unsigned long)r->cache_unchanged,(unsigned long)c->flags,
            (unsigned long)c->cpu_mhz,(unsigned long)c->brightness,(unsigned long)c->volume,
            (unsigned long)c->muted,(unsigned long)c->codec,(unsigned long)c->rate,
            (unsigned long)c->channels,(unsigned long)row->stable_ms,(unsigned long)row->playing_ms);
    }
    return !ferror(file);
}
#endif
