#include "battery_gauge.h"
#include <assert.h>
#include <string.h>
#if NDMEDIA_BATTERY_GAUGE_TEST
static const BatteryGaugePoint points[]={{3500,0},{3600,300},{3800,700},{4000,1000}};
static const BatteryGaugeProfile profile={points,4,26,0,396,80,3,44100,2,"Synthetic test profile"};
static BatteryCalibrationContext context={.flags=13,.cpu_mhz=396,.volume=80,.codec=3,.rate=44100,.channels=2};
static BatteryFreshReading reading;
static BatteryGaugeInfo info;
static uint32_t sequence;
static void fresh(uint32_t now,unsigned mv)
{
    reading=(BatteryFreshReading){.sequence=++sequence,.now_ms=now,.millivolts=mv,
        .charge_mode=1,.valid=true,.restored=1,.cache_unchanged=1};
    battery_gauge_update(now,&reading,&context);
    assert(battery_gauge_info(now,&info));
}
static void mismatch(BatteryCalibrationContext other)
{
    battery_gauge_update(reading.now_ms,NULL,&other);
    assert(battery_gauge_info(reading.now_ms,&info) && !info.percent_valid);
}
#endif
int main(void)
{
#if !NDMEDIA_BATTERY_GAUGE_TEST
    BatteryGaugeInfo info={.voltage_valid=true};
    assert(!battery_gauge_init(NULL));battery_gauge_update(0,NULL,NULL);
    assert(!battery_gauge_info(0,&info) && !info.voltage_valid && !info.percent_valid);
    FILE *f=tmpfile();assert(f);battery_gauge_debug(f);assert(!ftell(f));fclose(f);
    puts("PASS production battery gauge compiled out: no estimate or log output");
#else
    assert(!battery_gauge_init(NULL));
    BatteryGaugeProfile bad=profile;bad.count=1;assert(!battery_gauge_init(&bad));
    BatteryGaugePoint broken[]={{4000,100},{3900,1000}};bad=profile;bad.points=broken;bad.count=2;
    assert(!battery_gauge_init(&bad));broken[1]=(BatteryGaugePoint){4000,1000};assert(!battery_gauge_init(&bad));
    broken[1]=(BatteryGaugePoint){4100,99};assert(!battery_gauge_init(&bad));
    broken[1]=(BatteryGaugePoint){4100,1001};assert(!battery_gauge_init(&bad));
    bad=profile;bad.display_offset_mv=101;assert(!battery_gauge_init(&bad));
    const unsigned mv[]={3400,3500,3600,3700,3800,3900,4200},pct[]={0,0,300,500,700,850,1000};
    for(unsigned i=0;i<sizeof(mv)/sizeof(*mv);++i) {
        assert(battery_gauge_init(&profile));fresh(100,mv[i]);
        assert(info.percent_valid && info.percent_tenths==pct[i]);
    }
    /* A calibrated display switch preserves the same reference voltage. */
    battery_gauge_init(&profile);context.flags=13;fresh(0,3700);
    context.flags=9;fresh(5000,3674);
    assert(info.percent_valid && info.adjusted && info.reference_mv==3700 && info.percent_tenths==500);
    battery_gauge_update(5500,&reading,&context);battery_gauge_info(5500,&info);
    assert(info.reference_mv==3700 && info.age_ms==500);
    context.flags=13;fresh(10000,3700);assert(info.reference_mv==3700 && !info.adjusted);
    /* Unsupported load changes never reuse an apparently live percentage. */
    BatteryCalibrationContext other=context;other.cpu_mhz=456;mismatch(other);
    battery_gauge_init(&profile);fresh(0,3700);other=context;other.volume=79;mismatch(other);
    battery_gauge_init(&profile);fresh(0,3700);other=context;other.codec=2;mismatch(other);
    battery_gauge_init(&profile);fresh(0,3700);other=context;other.flags=5;mismatch(other);
    battery_gauge_init(&profile);fresh(0,3700);other=context;other.flags=9;other.brightness=179;mismatch(other);
    battery_gauge_init(&profile);fresh(0,3700);other=context;other.flags|=2;mismatch(other);
    battery_gauge_init(&profile);fresh(0,3700);reading.sequence=++sequence;reading.charge_mode=2;
    battery_gauge_update(0,&reading,&context);battery_gauge_info(0,&info);
    assert(info.voltage_valid && info.charging && !info.percent_valid);
    reading.sequence=++sequence;reading.charge_mode=6;
    battery_gauge_update(0,&reading,&context);battery_gauge_info(0,&info);
    assert(info.voltage_valid && !info.charging && info.checking && !info.percent_valid);
    reading.sequence=++sequence;reading.charge_mode=1;reading.cache_unchanged=0;
    battery_gauge_update(0,&reading,&context);assert(!battery_gauge_info(0,&info));
    /* Filter suppresses quantization/rebounds; long gaps reset its history. */
    battery_gauge_init(&profile);fresh(0,3600);fresh(5000,3980);
    assert(info.reference_mv>3600 && info.reference_mv<3700);
    assert(!battery_gauge_info(20001,&info) && !info.percent_valid);
    fresh(30000,3980);assert(info.reference_mv==3980);
    battery_gauge_init(&profile);fresh(UINT32_MAX-1000U,3700);
    assert(battery_gauge_info(4000,&info) && info.age_ms==5001);
    reading.sequence=++sequence;reading.now_ms=0;battery_gauge_update(20000,&reading,&context);
    assert(!battery_gauge_info(20000,&info));
    FILE *f=tmpfile();assert(f);battery_gauge_debug(f);assert(ftell(f)>0 && ftell(f)<1024);fclose(f);
    puts("PASS experimental gauge: supplied nonlinear profile, display compensation, load/charging/cache/freshness guards, smoothing and clock wrap");
#endif
    return 0;
}
