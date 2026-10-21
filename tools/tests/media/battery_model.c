#include "battery_model.h"
#include "battery_curve.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t now,rtc=20000U*86400U,sequence;
static BatteryNativeSnapshot native={
 .capabilities=BATTERY_NATIVE_PERCENT|BATTERY_NATIVE_CHARGE_MODE|BATTERY_NATIVE_THRESHOLDS|BATTERY_NATIVE_CALIBRATION,
 .percent=100,.charge_mode=1,.calibration_fingerprint=123,
 .levels={100,75,50,25,10,2,0},
 .thresholds={{3900,0},{3800,4000},{3740,3900},{3650,3840},{3500,3800},{3400,3750},{0,3600}}};
static void sample(BatteryModel *b,unsigned mv,unsigned mode,unsigned key,unsigned dt)
{
 now+=dt;rtc+=dt/1000;native.charge_mode=mode;battery_model_native(b,now,&native);
 battery_model_sample(b,now,rtc,++sequence,mv,mode,key);
}
static BatteryMeterInfo info(BatteryModel *b)
{BatteryMeterInfo o;assert(battery_model_info(b,now,&o));return o;}
static uint32_t checksum(const uint8_t *p,unsigned n)
{uint32_t c=UINT32_MAX;for(unsigned i=0;i<n;++i){c^=p[i];for(unsigned j=0;j<8;++j)c=(c>>1)^(0xedb88320U&-(c&1U));}return ~c;}
static void le32(uint8_t *p,uint32_t v)
{for(unsigned i=0;i<4;++i){p[i]=(uint8_t)v;v>>=8;}}
static void cycle(BatteryModel *b,unsigned rate,unsigned key)
{
 for(unsigned i=0;i<20;++i)sample(b,3964,1,key,5000);
 for(unsigned mv=3964;mv>=3500;--mv)sample(b,mv,1,key,rate);
}
int main(void)
{
 unsigned distinct=0,previous=0;
 for(unsigned mv=2500;mv<=4500;++mv) {
  unsigned p=battery_curve_percent(mv);assert(p>=previous && p<=1000);
  if(p!=previous)++distinct;
  previous=p;
 }
 assert(distinct>400 && !battery_curve_percent(3498) && battery_curve_percent(3964)==950 && battery_curve_percent(4200)==1000);
 assert(battery_curve_percent(3700)>550 && battery_curve_percent(3700)<590);
 assert(battery_curve_percent(3800)>735 && battery_curve_percent(3800)<770);
 assert(battery_curve_percent(3900)>900 && battery_curve_percent(3900)<935);
 assert(battery_curve_percent(4164)<995 && battery_curve_percent(4040)<980);
 assert(!battery_curve_usable(3500,3500));
 BatteryModel b;battery_model_init(&b);BatteryMeterInfo out;
 assert(!battery_model_info(&b,0,&out));battery_model_native(&b,0,&native);
 assert(battery_model_info(&b,0,&out) && !out.percent_valid); /* no cached OS100 flash */
 /* A continuous useful estimate exists on the first valid fresh sample. */
 sample(&b,3700,1,1,5000);out=info(&b);
 assert(out.percent_valid && out.estimated && out.source==BATTERY_METER_GENERAL &&
  out.percent_tenths>540 && out.percent_tenths<590 && out.os_percent==100);
 assert(!b.profile.loads[0].segments && !b.profile.loads[0].upper_mv);
 /* One isolated fragment cannot alter the default, even with an extreme
  * rate. Local estimates consume raw observations, never their own output. */
 for(unsigned i=0;i<10;++i)sample(&b,3700,1,1,5000);
 BatteryLoad *isolated=b.profile.loads;
 isolated->segments=1;isolated->observations[60]=1;
 isolated->rate[60]=300000;isolated->day[60]=(uint16_t)(rtc/86400U);
 b.display_valid=false;out=info(&b);
 assert(out.source==BATTERY_METER_GENERAL && out.percent_tenths>540 && out.percent_tenths<590);
 /* Cached samples do not train. Load steps do not become drain segments. */
 battery_model_init(&b);native.voltage_mv=3700;native.capabilities|=BATTERY_NATIVE_CACHED_VOLTAGE;
 for(unsigned i=0;i<20;++i)battery_model_native(&b,i*1000,&native);
 assert(!b.profile.dirty && !b.profile.loads[0].key);
 sample(&b,3800,1,11,5000);assert(info(&b).source==BATTERY_METER_GENERAL);
 for(unsigned i=0;i<30;++i)sample(&b,3800,1,11,5000);
 unsigned before=info(&b).percent_tenths;
 battery_model_context(&b,now+1,22);sample(&b,3700,1,22,5000);
 out=info(&b);assert(out.percent_tenths==before && !b.anchor_valid);
 for(unsigned i=0;i<30;++i){sample(&b,3700,1,22,5000);out=info(&b);}
 assert(out.percent_tenths<before && out.percent_tenths>500);
 sample(&b,4200,2,22,5000);out=info(&b);
 assert(out.charging && !out.estimated && !b.anchor_valid);
 sample(&b,4100,6,22,5000);assert(info(&b).checking && !b.anchor_valid);
 sample(&b,3700,1,22,5000);out=info(&b);
 assert(out.source==BATTERY_METER_GENERAL && out.percent_tenths<590);
 /* Repeated actual time/voltage observations may refine partial ranges.
  * An OS100 reading is never promoted to a known-full observation. */
 battery_model_init(&b);now=0;cycle(&b,5000,11);
 BatteryLoad *p=b.profile.loads;assert(p->segments>20 && !p->upper_mv);
 for(unsigned i=0;i<3;++i)cycle(&b,5000,11);
 out=info(&b);assert(out.source==BATTERY_METER_LEARNED && out.coverage_percent>50);
 /* Corrupt/atypical data with consistent counts is still bounded relative
  * to the prior instead of being allowed to replace the entire curve. */
 uint32_t retained_rate=p->rate[65];uint8_t retained_count=p->observations[65];
 sample(&b,3800,1,11,5000);
 for(unsigned i=0;i<20;++i)sample(&b,3800,1,11,5000);
 b.display_valid=false;unsigned baseline=info(&b).percent_tenths;
 p->rate[65]=300000;p->observations[65]=255;
 b.display_valid=false;unsigned poisoned=info(&b).percent_tenths;
 assert(poisoned+30>=baseline && poisoned<=baseline+30);
 p->rate[65]=retained_rate;p->observations[65]=retained_count;
 uint32_t young=p->rate[65];assert(young>=4900 && young<=5500);
 for(unsigned i=0;i<8;++i)cycle(&b,2500,11);
 assert(p->rate[65]<young*3/5 && p->rate[65]>=2400);
 sample(&b,3900,1,22,5000);assert(info(&b).source==BATTERY_METER_GENERAL);
 unsigned segments=b.profile.loads[1].segments;
 sample(&b,3700,1,22,300000);assert(b.profile.loads[1].segments==segments);
 sample(&b,3600,2,22,5000);assert(b.profile.loads[1].segments==segments);
 rtc-=3600;sample(&b,3500,1,22,5000);assert(b.profile.loads[1].segments==segments);
 uint8_t bytes[BATTERY_PROFILE_BYTES];BatteryProfile restored={0},old;
 size_t n=battery_profile_encode(&b.profile,bytes,sizeof(bytes),UINT32_MAX);
 assert(n && n<3200 && battery_profile_decode(&restored,bytes,n));
 assert(restored.generation==UINT32_MAX && restored.binding==b.profile.binding && !restored.dirty);
 old=restored;bytes[107]^=0x40;assert(!battery_profile_decode(&restored,bytes,n));
 assert(!memcmp(&old,&restored,sizeof(old)));bytes[107]^=0x40;
 for(size_t cut=0;cut<n;++cut)assert(!battery_profile_decode(&restored,bytes,cut));
 assert(!battery_profile_decode(&restored,bytes,n+1));
 assert(battery_profile_newer(0,UINT32_MAX) && !battery_profile_newer(UINT32_MAX,0));
 ++native.calibration_fingerprint;battery_model_native(&b,now,&native);
 for(unsigned i=0;i<BATTERY_LOADS;++i)assert(!b.profile.loads[i].key);
 battery_model_init(&b);now=0;cycle(&b,5000,11);cycle(&b,5000,11);cycle(&b,5000,11);
 rtc+=61U*86400U;sample(&b,3800,1,11,5000);
 assert(info(&b).source==BATTERY_METER_GENERAL && !b.profile.loads[0].segments);
 assert(!battery_model_info(&b,now+15001,&out));
 battery_model_reset(&b);assert(b.profile.dirty && !b.profile.loads[0].key);
 /* Last-run summary keeps actual accepted raw voltages, separating library
  * and playback peaks and excluding raised charging voltage. */
 battery_model_init(&b);sample(&b,4020,1,0,5000);sample(&b,4000,1,11,5000);
 sample(&b,4200,2,11,5000);sample(&b,3900,1,11,5000);out=info(&b);
 assert(b.run.valid_samples==4 && b.run.discharge_samples==3 && b.run.playback_samples==2);
 assert(b.run.max_mv==4020 && b.run.min_mv==3900 && b.run.playback_max_mv==4000 && b.run.playback_min_mv==3900);
 uint32_t count=b.run.valid_samples;
 battery_model_sample(&b,now,rtc,sequence,4300,1,11);assert(b.run.valid_samples==count && b.run.max_mv==4020);
 b.profile.last_run=b.run;n=battery_profile_encode(&b.profile,bytes,sizeof(bytes),7);
 assert(n==2968 && battery_profile_decode(&restored,bytes,n));
 assert(restored.last_run.max_mv==4020 && restored.last_run.playback_max_mv==4000 && restored.last_run.discharge_samples==3);
 /* Existing v1 profiles migrate without inventing retrospective min/max. */
 le32(bytes+4,1);le32(bytes+8,2904);le32(bytes+2900,checksum(bytes,2900));
 assert(battery_profile_decode(&restored,bytes,2904));
 assert(restored.generation==7 && !restored.last_run.valid_samples && !restored.last_run.max_mv);
 puts("PASS continuous smoothed default, no required calibration, conservative partial learning, load/charge/gap exclusion, ageing, expiry and CRC safety");
 return 0;
}
