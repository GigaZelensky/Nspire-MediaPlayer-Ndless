#ifndef NMEDIA_BATTERY_DIAGNOSTICS_H
#define NMEDIA_BATTERY_DIAGNOSTICS_H
#include "../media_config.h"
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#ifndef NDVIDEO_BATTERY_TIMINGS
#define NDVIDEO_BATTERY_TIMINGS 0
#endif
#ifndef NDVIDEO_BATTERY_ACQUISITION
#define NDVIDEO_BATTERY_ACQUISITION 0
#endif
#if NDVIDEO_BATTERY_ACQUISITION && !NDVIDEO_BATTERY_TIMINGS && !NDMEDIA_BATTERY_SERVICE
#error Battery acquisition requires the guarded battery service or diagnostic recorder
#endif

/* A copy of one completed attempt; consumers must check valid rather than
 * treating an old or rejected conversion as a new voltage. No hardware I/O. */
typedef struct {
    uint32_t sequence,now_ms,rtc,millivolts,cached_mv,raw,os_percent,charge_mode;
    uint32_t guard,result,restored,cache_unchanged;
    bool valid;
} BatteryFreshReading;
typedef struct {
    uint32_t mode,age_ms;
    bool charging,checking;
} BatteryPowerInfo;

/* Voltage service, not a charge estimate. Active acquisition accepts only
 * the exact CX II firmware verified by the separate hardware probe. Recording
 * is independently default-off; a quiet service retains only its last result. */
#if NDVIDEO_BATTERY_TIMINGS || NDMEDIA_BATTERY_SERVICE
void battery_diagnostics_init(void);
void battery_diagnostics_tick(uint32_t now_ms);
bool battery_diagnostics_power(uint32_t now_ms,BatteryPowerInfo *);
#else
static inline void battery_diagnostics_init(void) { }
static inline void battery_diagnostics_tick(uint32_t now_ms) { (void)now_ms; }
static inline bool battery_diagnostics_power(uint32_t now,BatteryPowerInfo *out)
{(void)now;if(out)*out=(BatteryPowerInfo){0};return false;}
#endif
#if NDVIDEO_BATTERY_TIMINGS
void battery_diagnostics_debug(FILE *file);
#else
static inline void battery_diagnostics_debug(FILE *file) { (void)file; }
#endif
#if NDVIDEO_BATTERY_ACQUISITION || NDMEDIA_BATTERY_SERVICE
/* Explicit foreground maintenance point only, after the audio pump returns.
 * Never call from a clock callback, native operation or USB service callback. */
bool battery_diagnostics_acquisition_due(uint32_t now_ms);
bool battery_diagnostics_acquire(uint32_t now_ms);
bool battery_diagnostics_latest(BatteryFreshReading *reading);
#else
static inline bool battery_diagnostics_acquisition_due(uint32_t now_ms) { (void)now_ms;return false; }
static inline bool battery_diagnostics_acquire(uint32_t now_ms) { (void)now_ms;return false; }
static inline bool battery_diagnostics_latest(BatteryFreshReading *reading) { (void)reading;return false; }
#endif
#endif
