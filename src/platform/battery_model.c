#include "battery_model.h"
#include "battery_curve.h"
#include <string.h>

enum { BAND_MV=20, MIN_MV=2500, MAX_MV=4500, EXPIRE_DAYS=60,
       SETTLE_MS=30000, SEGMENT_MS=60000, MAX_GAP_MS=15000 };
static uint32_t mix(uint32_t h,uint32_t v){return (h^v)*16777619U;}
static uint32_t binding(const BatteryNativeSnapshot *s)
{
    uint32_t h=2166136261U;
    for(unsigned i=0;i<7;++i) {
        h=mix(h,s->levels[i]);h=mix(h,s->thresholds[i][0]);h=mix(h,s->thresholds[i][1]);
    }
    return mix(h,s->calibration_fingerprint); /* ADC trim compatibility, not battery identity */
}
static uint32_t reserve(const BatteryModel *b)
{
    if(b->native.capabilities&BATTERY_NATIVE_THRESHOLDS)
        for(unsigned i=1;i<7;++i)if(b->native.levels[i]<=2) {
            uint32_t v=b->native.thresholds[i-1][0];
            if(v>=MIN_MV && v<BATTERY_CURVE_FULL_MV-200)return v;
            break;
        }
    return BATTERY_CURVE_EMPTY_MV;
}
static unsigned band(uint32_t mv)
{return mv<=MIN_MV?0:mv>=MAX_MV?BATTERY_BANDS-1:(mv-MIN_MV)/BAND_MV;}
static bool expired(uint32_t now,uint32_t before)
{return !now || !before || now<before || now-before>EXPIRE_DAYS;}
void battery_model_init(BatteryModel *b){memset(b,0,sizeof(*b));}
void battery_model_reset(BatteryModel *b)
{
    uint32_t generation=b->profile.generation,bound=b->profile.binding;
    memset(&b->profile,0,sizeof(b->profile));
    b->profile.generation=generation;b->profile.binding=bound;b->profile.dirty=true;
    b->anchor_valid=false;b->load_key=0;
}
void battery_model_native(BatteryModel *b,uint32_t now,const BatteryNativeSnapshot *s)
{
    if(!s || !(s->capabilities&BATTERY_NATIVE_PERCENT) || s->percent>100 || s->level_index>=7)return;
    b->native=*s;b->native_ms=now;b->native_valid=true;
    if((s->capabilities&(BATTERY_NATIVE_THRESHOLDS|BATTERY_NATIVE_CALIBRATION))==
        (BATTERY_NATIVE_THRESHOLDS|BATTERY_NATIVE_CALIBRATION)) {
        uint32_t id=binding(s);
        if(b->profile.binding && id!=b->profile.binding)battery_model_reset(b);
        b->profile.binding=id;
    }
}
void battery_model_power(BatteryModel *b,uint32_t now,bool known,bool external,bool host)
{
    if(!known && !b->power_seen)return;
    if(known!=b->power_known || (known && (external!=b->power_external || host!=b->power_host))) {
        b->stable_ms=now;b->anchor_valid=false;b->load_key=0;
    }
    if(known)b->power_seen=true;
    b->power_known=known;b->power_external=known && external;b->power_host=known && host;
}
static uint32_t power_mode(const BatteryModel *b,uint32_t reported)
{
    /* Docking power is independent of USB. Otherwise a measured USB source
     * overrides stale charge-task metadata; an unreadable source is unknown. */
    if(b->power_seen) {
        if(b->native.charge_mode==4 || b->native.charge_mode==5)return b->native.charge_mode;
        if(!b->power_known)return 0;
        if(!b->power_external)return 1;
        if(reported<2 || reported>6)return 0;
    }
    return reported;
}
void battery_model_context(BatteryModel *b,uint32_t now,uint32_t key)
{
    if(!b->context_valid || b->context_key!=key) {
        b->context_valid=true;b->context_key=key;b->context_ms=b->stable_ms=now;
        b->load_key=0;b->anchor_valid=false;
        /* Keep presentation continuous through a load change. No voltage
         * step is silently relabeled as observed battery consumption. */
    }
}
static BatteryLoad *load(BatteryModel *b,uint32_t key,uint32_t day)
{
    BatteryLoad *oldest=b->profile.loads;
    for(unsigned i=0;i<BATTERY_LOADS;++i) {
        BatteryLoad *p=b->profile.loads+i;
        if(p->key==key) {
            if(expired(day,p->last_day)){memset(p,0,sizeof(*p));p->key=key;b->profile.dirty=true;}
            p->last_day=day;return p;
        }
        if(oldest->key && (!p->key || p->last_day<oldest->last_day))oldest=p;
    }
    memset(oldest,0,sizeof(*oldest));oldest->key=key;oldest->last_day=day;
    b->profile.dirty=true;return oldest;
}
void battery_model_sample(BatteryModel *b,uint32_t now,uint32_t rtc,uint32_t seq,
                          uint32_t mv,uint32_t mode,uint32_t key)
{
    if(!seq || seq==b->last_sequence || mv<MIN_MV || mv>MAX_MV)return;
    mode=power_mode(b,mode);
    battery_model_context(b,now,key);b->last_sequence=seq;
    bool continuous=b->fresh_valid && now-b->reading_ms<=MAX_GAP_MS && rtc>=b->last_rtc &&
        rtc-b->last_rtc<=MAX_GAP_MS/1000+1 && mode==b->fresh_mode;
    b->last_rtc=rtc;b->reading_ms=now;b->fresh_valid=true;b->fresh_mode=mode;
    if(b->run.valid_samples!=UINT32_MAX)++b->run.valid_samples;
    b->filter_q8=continuous?(mv*256U+8*b->filter_q8)/9:mv*256U;
    b->filtered_mv=(b->filter_q8+128)/256;
    if(!continuous){b->stable_ms=now;b->anchor_valid=false;}
    if(!b->native_valid || now-b->native_ms>MAX_GAP_MS || mode!=1 ||
        !(b->native.capabilities&BATTERY_NATIVE_CHARGE_MODE) || power_mode(b,b->native.charge_mode)!=1) {
        b->anchor_valid=false;b->load_key=0;return;
    }
    BatteryRunSummary *r=&b->run;
    if(!r->discharge_samples) {
        r->min_mv=r->max_mv=r->first_mv=mv;r->first_rtc=rtc;r->first_ms=now;
    }
    if(mv<r->min_mv)r->min_mv=mv;
    if(mv>r->max_mv)r->max_mv=mv;
    r->last_mv=mv;r->last_rtc=rtc;r->last_ms=now;
    if(r->discharge_samples!=UINT32_MAX)++r->discharge_samples;
    if(key && (int32_t)(now-b->context_ms)>=0) {
        if(!r->playback_samples)r->playback_min_mv=r->playback_max_mv=mv;
        if(mv<r->playback_min_mv)r->playback_min_mv=mv;
        if(mv>r->playback_max_mv)r->playback_max_mv=mv;
        if(r->playback_samples!=UINT32_MAX)++r->playback_samples;
    }
    uint32_t day=rtc/86400U;
    if(!key || !day || !(b->native.capabilities&BATTERY_NATIVE_CALIBRATION) ||
        (int32_t)(now-b->context_ms)<0) {b->anchor_valid=false;b->load_key=0;return;}
    BatteryLoad *p=load(b,key,day);
    bool rebound=continuous && b->load_key==key && mv>b->last_mv+8;
    if(rebound){b->stable_ms=now;b->anchor_valid=false;}
    b->load_key=key;b->last_mv=mv;
    if(now-b->stable_ms<SETTLE_MS)return;
    /* Do not promote OS100 to a full-charge anchor. The default endpoints
     * remain assumptions; ordinary partial runs refine the measured shape. */
    if(!b->anchor_valid){b->anchor_ms=now;b->anchor_mv=mv;b->anchor_valid=true;return;}
    if(mv>b->anchor_mv+8 || now-b->anchor_ms>600000U) {
        b->anchor_ms=now;b->anchor_mv=mv;return;
    }
    if(now-b->anchor_ms<SEGMENT_MS || mv+8>b->anchor_mv)return;
    uint32_t elapsed=now-b->anchor_ms,drop=b->anchor_mv-mv,rate=elapsed/drop;
    b->anchor_ms=now;b->anchor_mv=mv;
    if(rate<100 || rate>300000 || drop>100)return;
    unsigned bottom=band(mv),top=band(mv+drop-1);
    for(unsigned i=bottom;i<=top;++i) {
        if(!p->observations[i] || expired(day,p->day[i])) {
            p->rate[i]=rate;p->observations[i]=1;
        } else {
            bool consistent=rate<=2*p->rate[i] && 2*rate>=p->rate[i];
            p->rate[i]=(3*p->rate[i]+rate+2)/4;
            if(consistent && p->observations[i]<255)++p->observations[i];
            else if(!consistent && p->observations[i]>1)--p->observations[i];
        }
        p->day[i]=(uint16_t)day;
    }
    if(p->segments!=UINT32_MAX)++p->segments;
    b->profile.dirty=true;
}
static unsigned evidence(const BatteryLoad *p,unsigned i,uint32_t day)
{
    if(p->observations[i]<3 || expired(day,p->day[i]))return 0;
    unsigned n=p->observations[i]-2;return n>8?8:n;
}
/* Fit a time scale to qualified observed bands, not to our own displayed %.
 * The median rejects one extreme segment. Unseen bands retain the default
 * shape; observed ones blend gradually, capped at half weight and a2x ratio. */
static bool refine(const BatteryLoad *p,uint32_t day,uint32_t floor,uint32_t mv,
                   uint32_t *percent,uint32_t *minutes,uint32_t *coverage)
{
    uint32_t scales[BATTERY_BANDS];unsigned count=0,first=band(floor),last=band(BATTERY_CURVE_FULL_MV-1);
    for(unsigned i=first;i<=last;++i)if(evidence(p,i,day)) {
        uint32_t lo=MIN_MV+i*BAND_MV,hi=lo+BAND_MV;
        if(lo<floor)lo=floor;
        if(hi>BATTERY_CURVE_FULL_MV)hi=BATTERY_CURVE_FULL_MV;
        uint32_t delta=battery_curve_usable(hi,floor)-battery_curve_usable(lo,floor);
        if(!delta)continue;
        uint32_t scale=(uint32_t)((uint64_t)p->rate[i]*(hi-lo)/delta);
        unsigned j=count;while(j && scales[j-1]>scale){scales[j]=scales[j-1];--j;}
        scales[j]=scale;++count;
    }
    *coverage=100*count/(last-first+1);if(!count)return false;
    uint32_t scale=scales[count/2];uint64_t total=0,remaining=0;
    if(!scale)return false;
    for(unsigned i=first;i<=last;++i) {
        uint32_t lo=MIN_MV+i*BAND_MV,hi=lo+BAND_MV;
        if(lo<floor)lo=floor;
        if(hi>BATTERY_CURVE_FULL_MV)hi=BATTERY_CURVE_FULL_MV;
        uint32_t end=mv<hi?mv:hi;if(end<lo)end=lo;
        uint32_t a=battery_curve_usable(lo,floor),z=battery_curve_usable(hi,floor),v=battery_curve_usable(end,floor);
        uint64_t prior=(uint64_t)scale*(z-a),partial=(uint64_t)scale*(v-a);
        unsigned weight=evidence(p,i,day);
        if(weight && prior) {
            uint64_t observed=(uint64_t)p->rate[i]*(hi-lo);
            if(observed<prior/2)observed=prior/2;
            if(observed>prior*2)observed=prior*2;
            uint64_t local=observed*(end-lo)/(hi-lo);
            total+=(8*prior+weight*observed)/(8+weight);
            remaining+=(8*partial+weight*local)/(8+weight);
        } else {total+=prior;remaining+=partial;}
    }
    if(!total)return false;
    *percent=(uint32_t)((remaining*1000+total/2)/total);
    *minutes=*coverage>=20 && p->segments>=5?(uint32_t)(remaining/60000):0;return true;
}
bool battery_model_info(BatteryModel *b,uint32_t now,BatteryMeterInfo *out)
{
    if(!out)return false;
    *out=(BatteryMeterInfo){0};
    out->source=BATTERY_METER_GENERAL;
    out->external_power=b->power_known && b->power_external;
    out->usb_host=b->power_known && b->power_host;
    /* Keep the last genuine estimate through source changes and missing
     * samples. Coarse OS levels are diagnostic metadata, never a fallback. */
    if(b->display_valid) {
        out->percent_valid=out->estimated=true;out->percent_tenths=(b->display_q8+128)/256;
    }
    bool native_recent=b->native_valid && now-b->native_ms<=MAX_GAP_MS;
    uint32_t mode=native_recent?power_mode(b,b->native.charge_mode):0;
    out->charging=native_recent && (b->native.capabilities&BATTERY_NATIVE_CHARGE_MODE) && mode>=2 && mode<=5;
    out->checking=native_recent && (b->native.capabilities&BATTERY_NATIVE_CHARGE_MODE) && mode==6;
    bool recent=b->fresh_valid && now-b->reading_ms<=15000;
    out->voltage_valid=recent;out->millivolts=recent?b->filtered_mv:0;
    out->os_percent=native_recent?b->native.percent:UINT32_MAX;
    if(!recent)return native_recent || out->percent_valid || out->external_power || out->usb_host;
    /* A fresh voltage can seed the general estimate even on a PC cable.
     * Charging voltage is only an estimate, never discharge-learning data
     * or evidence for a learned remaining runtime. */
    bool discharging=native_recent && !out->external_power && !out->charging && !out->checking &&
        b->fresh_mode==1 && (b->native.capabilities&BATTERY_NATIVE_CHARGE_MODE) && mode==1;
    /* The voltage service authenticates its own ADC and per-device trim.
     * Its first genuine result need not wait for the independent native
     * metadata scan. Only the default curve is used until that scan finishes. */
    uint32_t floor=native_recent?reserve(b):BATTERY_CURVE_EMPTY_MV;
    uint32_t target=battery_curve_usable(b->filtered_mv,floor);
    out->source=BATTERY_METER_GENERAL;out->percent_valid=out->estimated=true;
    const BatteryLoad *p=NULL;
    if(discharging && (b->native.capabilities&BATTERY_NATIVE_CALIBRATION) && b->load_key && now-b->stable_ms>=SETTLE_MS)
        for(unsigned i=0;i<BATTERY_LOADS;++i)if(b->profile.loads[i].key==b->load_key)p=b->profile.loads+i;
    out->learning=p!=NULL;
    if(p && !expired(b->last_rtc/86400U,p->last_day) &&
       refine(p,b->last_rtc/86400U,floor,b->filtered_mv,&target,&out->remaining_minutes,&out->coverage_percent))
        out->source=BATTERY_METER_LEARNED;
    if(!b->display_valid || b->filtered_mv<=floor) {
        b->display_q8=target*256U;b->display_ms=now;b->display_valid=true;
    } else if(now-b->display_ms>=1000) {
        uint32_t elapsed=now-b->display_ms;
        if(now-b->stable_ms>=SETTLE_MS) {
            if(elapsed>15000)elapsed=15000;
            int64_t delta=(int64_t)target*256-b->display_q8;
            b->display_q8=(uint32_t)((int64_t)b->display_q8+delta*elapsed/(30000U+elapsed));
        }
        b->display_ms=now;
    }
    out->percent_tenths=(b->display_q8+128)/256;
    b->run.final_percent_tenths=out->percent_tenths;b->run.final_source=out->source;
    return true;
}
static uint32_t crc(const uint8_t *s,size_t n)
{
    uint32_t v=UINT32_MAX;
    for(size_t i=0;i<n;++i) {
        v^=s[i];for(unsigned j=0;j<8;++j)v=(v>>1)^(0xedb88320U&-(v&1U));
    }
    return ~v;
}
static void put(uint8_t **p,uint32_t v,unsigned n)
{for(unsigned i=0;i<n;++i){*(*p)++=(uint8_t)v;v>>=8;}}
static uint32_t get(const uint8_t **p,unsigned n)
{uint32_t v=0;for(unsigned i=0;i<n;++i)v|=(uint32_t)*(*p)++<<(8*i);return v;}
enum { RECORD_V1_BYTES=24+BATTERY_LOADS*(20+BATTERY_BANDS*7), RECORD_BYTES=RECORD_V1_BYTES+64 };
size_t battery_profile_encode(const BatteryProfile *b,uint8_t *data,size_t n,uint32_t generation)
{
    if(!b || !data || n<RECORD_BYTES)return 0;
    uint8_t *p=data;
    put(&p,0x47424d4e,4);put(&p,2,4);put(&p,RECORD_BYTES,4);
    put(&p,generation,4);put(&p,b->binding,4);
    for(unsigned k=0;k<BATTERY_LOADS;++k) {
        const BatteryLoad *m=b->loads+k;
        put(&p,m->key,4);put(&p,m->last_day,4);put(&p,m->upper_mv,4);
        put(&p,m->upper_observations,4);put(&p,m->segments,4);
        for(unsigned i=0;i<BATTERY_BANDS;++i) {
            put(&p,m->rate[i],4);put(&p,m->day[i],2);put(&p,m->observations[i],1);
        }
    }
    const BatteryRunSummary *r=&b->last_run;
    const uint32_t fields[]={r->valid_samples,r->discharge_samples,r->min_mv,r->max_mv,r->first_mv,r->last_mv,
        r->first_rtc,r->last_rtc,r->first_ms,r->last_ms,r->playback_samples,r->playback_min_mv,
        r->playback_max_mv,r->final_percent_tenths,r->final_source,0};
    for(unsigned i=0;i<16;++i)put(&p,fields[i],4);
    put(&p,crc(data,RECORD_BYTES-4),4);return (size_t)(p-data);
}
bool battery_profile_decode(BatteryProfile *b,const uint8_t *data,size_t n)
{
    if(!b || !data || (n!=RECORD_BYTES && n!=RECORD_V1_BYTES))return false;
    const uint8_t *p=data,*tail=data+n-4;
    if(crc(data,n-4)!=get(&tail,4) || get(&p,4)!=0x47424d4e)return false;
    uint32_t version=get(&p,4);
    if(!((version==1 && n==RECORD_V1_BYTES) || (version==2 && n==RECORD_BYTES)) || get(&p,4)!=n)return false;
    BatteryProfile candidate={0};
    candidate.generation=get(&p,4);candidate.binding=get(&p,4);
    for(unsigned k=0;k<BATTERY_LOADS;++k) {
        BatteryLoad *m=candidate.loads+k;
        m->key=get(&p,4);m->last_day=get(&p,4);m->upper_mv=get(&p,4);
        m->upper_observations=get(&p,4);m->segments=get(&p,4);
        if(m->upper_mv && (m->upper_mv<MIN_MV || m->upper_mv>MAX_MV))return false;
        for(unsigned i=0;i<BATTERY_BANDS;++i) {
            m->rate[i]=get(&p,4);m->day[i]=(uint16_t)get(&p,2);m->observations[i]=(uint8_t)get(&p,1);
            if(m->observations[i] && (m->rate[i]<100 || m->rate[i]>300000))return false;
        }
    }
    if(version==2) {
        uint32_t f[16];for(unsigned i=0;i<16;++i)f[i]=get(&p,4);
        if(f[1]>f[0] || f[10]>f[1] || f[13]>1000 || f[14]>BATTERY_METER_LEARNED || f[15])return false;
        if(f[1] && (f[2]<MIN_MV || f[3]>MAX_MV || f[2]>f[3] ||
            f[4]<f[2] || f[4]>f[3] || f[5]<f[2] || f[5]>f[3]))return false;
        if(f[10] && (f[11]<f[2] || f[12]>f[3] || f[11]>f[12]))return false;
        candidate.last_run=(BatteryRunSummary){
            .valid_samples=f[0],.discharge_samples=f[1],.min_mv=f[2],.max_mv=f[3],.first_mv=f[4],.last_mv=f[5],
            .first_rtc=f[6],.last_rtc=f[7],.first_ms=f[8],.last_ms=f[9],.playback_samples=f[10],
            .playback_min_mv=f[11],.playback_max_mv=f[12],.final_percent_tenths=f[13],.final_source=f[14]};
    }
    *b=candidate;return true;
}
bool battery_profile_newer(uint32_t a,uint32_t b){return (int32_t)(a-b)>0;}
