#ifndef NMEDIA_BATTERY_GAUGE_H
#define NMEDIA_BATTERY_GAUGE_H
#include "battery_calibration.h"
#ifndef NDMEDIA_BATTERY_GAUGE_TEST
#define NDMEDIA_BATTERY_GAUGE_TEST 0
#endif
typedef struct {uint16_t millivolts,percent_tenths;} BatteryGaugePoint;
/* The caller retains this immutable measured profile. No universal curve is
 * built in; the private test build supplies its own generated header. */
typedef struct {
    const BatteryGaugePoint *points;
    unsigned count,display_offset_mv,on_brightness,cpu_mhz,volume,codec,rate,channels;
    const char *description;
} BatteryGaugeProfile;
typedef struct {
    bool voltage_valid,percent_valid,reference_load,adjusted,charging,checking;
    uint32_t age_ms,millivolts,reference_mv,percent_tenths;
} BatteryGaugeInfo;
#if NDMEDIA_BATTERY_GAUGE_TEST
bool battery_gauge_init(const BatteryGaugeProfile *);
void battery_gauge_update(uint32_t now_ms,const BatteryFreshReading *,const BatteryCalibrationContext *);
bool battery_gauge_info(uint32_t now_ms,BatteryGaugeInfo *);
void battery_gauge_debug(FILE *);
#else
static inline bool battery_gauge_init(const BatteryGaugeProfile *p){(void)p;return false;}
static inline void battery_gauge_update(uint32_t n,const BatteryFreshReading *r,const BatteryCalibrationContext *c)
{(void)n;(void)r;(void)c;}
static inline bool battery_gauge_info(uint32_t n,BatteryGaugeInfo *out)
{(void)n;if(out)*out=(BatteryGaugeInfo){0};return false;}
static inline void battery_gauge_debug(FILE *f){(void)f;}
#endif
#endif
