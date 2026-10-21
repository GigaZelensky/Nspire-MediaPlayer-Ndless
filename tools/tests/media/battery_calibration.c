#include "battery_calibration.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#if NDMEDIA_BATTERY_CALIBRATION
static BatteryCalibrationContext context={.flags=BATTERY_CAL_PLAYING|BATTERY_CAL_SCREEN_OFF|BATTERY_CAL_USB_SOURCE,
    .cpu_mhz=396,.volume=45,.codec=7,.rate=48000,.channels=2};
static uint32_t sequence;
static void sample(uint32_t now,uint32_t mv)
{
    BatteryFreshReading r={.sequence=++sequence,.now_ms=now,.rtc=1000+now/1000,
        .millivolts=mv,.cached_mv=4000,.raw=947,.os_percent=100,.charge_mode=1,
        .valid=true,.restored=1,.cache_unchanged=1};
    battery_calibration_tick(now,&r,&context);
}
static char *report(void)
{
    FILE *file=tmpfile();assert(file);assert(battery_calibration_report(file));
    long bytes=ftell(file);assert(bytes>0 && bytes<600000);
    char *text=calloc((size_t)bytes+1,1);assert(text);rewind(file);
    assert(fread(text,1,(size_t)bytes,file)==(size_t)bytes);assert(!fclose(file));return text;
}
#endif
int main(void)
{
    battery_calibration_init();
#if !NDMEDIA_BATTERY_CALIBRATION
    battery_calibration_start(0);battery_calibration_tick(1,NULL,NULL);
    FILE *file=tmpfile();assert(file);assert(!battery_calibration_report(file) && !ftell(file));fclose(file);
    assert(!battery_calibration_active() && !battery_calibration_stopped());
    puts("PASS production calibration is compiled out: no records or output");
#elif NDMEDIA_BATTERY_LOAD_TEST
    battery_calibration_start(1000);
    assert(battery_calibration_screen_target(1000)==0);
    assert(battery_calibration_screen_target(61000)==1);
    assert(battery_calibration_screen_target(121000)==0);
    for(uint32_t t=0;t<=180000;t+=250) {
        bool on=t>=60000 && t<120000;
        context.flags=BATTERY_CAL_PLAYING|BATTERY_CAL_USB_SOURCE|(on?0:BATTERY_CAL_SCREEN_OFF);
        context.brightness=on?179:0;
        if(t%5000==0)sample(t+1000,3900-t/10000);
        else battery_calibration_tick(t+1000,NULL,&context);
    }
    assert(battery_calibration_stopped() && !strcmp(battery_calibration_reason(),"load_complete"));
    assert(battery_calibration_screen_target(181000)==-1);
    char *text=report();assert(strstr(text,"# nMedia battery load comparison v1\n") &&
        strstr(text,"# rows=37\n") && strstr(text,"# attempts=37\n"));free(text);
    battery_calibration_init();battery_calibration_start(0);
    for(unsigned t=0;t<=30000;t+=5000)sample(t,3490);
    assert(battery_calibration_stopped() && !strcmp(battery_calibration_reason(),"low_voltage"));
    puts("PASS short load comparison: off/on/off plan, 5s records, automatic finish and retained low-voltage stop");
#else
    assert(!battery_calibration_active() && !battery_calibration_has_samples());
    battery_calibration_start(1000);sample(1000,4150);
    /* Duplicate/stale observations never become additional acquisitions. */
    BatteryFreshReading duplicate={.sequence=sequence,.now_ms=1000,.valid=true,.millivolts=1000};
    battery_calibration_tick(2000,&duplicate,&context);
    sample(6000,4140);sample(31000,4100);
    char *text=report();assert(strstr(text,"# attempts=3\n") && strstr(text,"# rows=2\n"));free(text);
    assert(!battery_calibration_stopped());
    /* A transient voltage dip or rejected sample cannot satisfy low hold. */
    sample(36000,3490);sample(41000,3600);sample(46000,3490);
    BatteryFreshReading bad={.sequence=++sequence,.now_ms=51000,.millivolts=3490,.charge_mode=1};
    battery_calibration_tick(51000,&bad,&context);sample(56000,3490);
    for(unsigned i=1;i<=5;++i){sample(56000+i*5000,3490);assert(!battery_calibration_stopped());}
    sample(86000,3490);assert(battery_calibration_stopped() && !strcmp(battery_calibration_reason(),"low_voltage"));
    text=report();assert(strstr(text,"# rejected=1\n") && strstr(text,"# end_reason=low_voltage\n"));free(text);
    /* There must be regular fresh points throughout low hold, not a gap. */
    battery_calibration_init();battery_calibration_start(0);sample(0,3490);sample(40000,3490);
    assert(!battery_calibration_stopped());
    /* Missing readings and charger state have explicit stop reasons. */
    battery_calibration_init();battery_calibration_start(0);battery_calibration_tick(120000,NULL,&context);
    assert(!strcmp(battery_calibration_reason(),"no_fresh_samples"));
    battery_calibration_init();battery_calibration_start(0);sample(0,4100);
    battery_calibration_abort();assert(battery_calibration_stopped());
    assert(!strcmp(battery_calibration_reason(),"playback_stopped"));
    battery_calibration_init();battery_calibration_start(0);sample(0,4150);
    battery_calibration_tick(120000,NULL,&context);assert(!strcmp(battery_calibration_reason(),"samples_lost"));
    battery_calibration_init();battery_calibration_start(0);
    BatteryFreshReading charging={.sequence=++sequence,.now_ms=0,.millivolts=4200,.charge_mode=2,.valid=true};
    battery_calibration_tick(0,&charging,&context);assert(!strcmp(battery_calibration_reason(),"charging"));
    /* Bounded memory even if every sample is rejected. All attempts counted. */
    battery_calibration_init();battery_calibration_start(0);
    for(unsigned i=0;i<4097;++i){bad.sequence=++sequence;bad.now_ms=i;battery_calibration_tick(i,&bad,&context);}
    assert(!strcmp(battery_calibration_reason(),"capacity"));text=report();
    assert(strstr(text,"# rows=4096\n") && strstr(text,"# attempts=4097\n") && strstr(text,"# rejected=4097\n"));free(text);
    /* Wrap-safe timer and no duplicate start/reset when switching songs. */
    battery_calibration_init();uint32_t start=UINT32_MAX-1000U;battery_calibration_start(start);
    sample(start,4100);battery_calibration_start(0);sample(start+30000U,4000);
    text=report();assert(strstr(text,"30000,") && strstr(text,"# rows=2\n"));free(text);
    battery_calibration_init();battery_calibration_start(0);sample(0,4100);
    battery_calibration_tick(86400000U,NULL,&context);assert(!strcmp(battery_calibration_reason(),"time_limit"));
    puts("PASS calibration: bounded RAM/report, 30s retention, all-attempt counts, low-voltage persistence, charger/freshness stops and wrap-safe timing");
#endif
    return 0;
}
