#ifndef NMEDIA_BATTERY_MODEL_H
#define NMEDIA_BATTERY_MODEL_H
#include "battery_native.h"
#include <stddef.h>

/* Ordinary-use learning of usable running time, not coulomb counting or a
 * chemical state-of-health measurement. A smoothed default playback curve is
 * refined only by qualified local voltage/time observations. */
enum { BATTERY_LOADS=4, BATTERY_BANDS=100, BATTERY_PROFILE_BYTES=3200 };
typedef struct {
    /* upper fields retained for v1 file compatibility; the current learner
     * never equates the cached OS100 level with a known-full observation. */
    uint32_t key,last_day,upper_mv,upper_observations,segments;
    uint32_t rate[BATTERY_BANDS]; /* milliseconds / mV under this load */
    uint16_t day[BATTERY_BANDS];
    uint8_t observations[BATTERY_BANDS];
} BatteryLoad;
typedef struct {
    /* Bounded last-run measurements, not a sample log. Discharge excludes
     * native charging/check modes; playback also requires a nonzero load key. */
    uint32_t valid_samples,discharge_samples,min_mv,max_mv,first_mv,last_mv;
    uint32_t first_rtc,last_rtc,first_ms,last_ms;
    uint32_t playback_samples,playback_min_mv,playback_max_mv;
    uint32_t final_percent_tenths,final_source;
} BatteryRunSummary;
typedef struct {
    uint32_t binding,generation;
    BatteryLoad loads[BATTERY_LOADS];
    BatteryRunSummary last_run;
    bool dirty;
} BatteryProfile;
typedef struct {
    BatteryProfile profile;
    BatteryNativeSnapshot native;
    BatteryRunSummary run;
    uint32_t native_ms,reading_ms,last_sequence,filtered_mv,filter_q8,fresh_mode;
    uint32_t load_key,context_key,context_ms,anchor_ms,anchor_mv,last_mv,stable_ms;
    uint32_t last_rtc,display_ms,display_q8;
    bool native_valid,fresh_valid,context_valid,anchor_valid,display_valid;
    bool power_seen,power_known,power_external,power_host;
} BatteryModel;
/* Keep the legacy OS source ID for stored-report compatibility. The live
 * meter never substitutes a coarse OS level for a voltage estimate. */
typedef enum { BATTERY_METER_OS, BATTERY_METER_GENERAL, BATTERY_METER_LEARNED } BatteryMeterSource;
typedef struct {
    BatteryMeterSource source;
    uint32_t millivolts,percent_tenths,remaining_minutes,coverage_percent;
    uint32_t os_percent; /* Diagnostics only; UINT32_MAX while unavailable. */
    bool percent_valid,voltage_valid,estimated,charging,checking,learning,external_power,usb_host;
} BatteryMeterInfo;

void battery_model_init(BatteryModel *);
void battery_model_native(BatteryModel *,uint32_t now,const BatteryNativeSnapshot *);
void battery_model_power(BatteryModel *,uint32_t now,bool known,bool external,bool host);
void battery_model_context(BatteryModel *,uint32_t now,uint32_t key);
/* key identifies a stable workload, including screen/brightness. Zero means
 * no qualified workload; cached ADC data must never enter this function.
 * The independently verified fresh ADC can seed the default estimate before
 * native metadata resolves. Learning still requires valid native metadata. */
void battery_model_sample(BatteryModel *,uint32_t now,uint32_t rtc,uint32_t sequence,
                          uint32_t millivolts,uint32_t charge_mode,uint32_t key);
bool battery_model_info(BatteryModel *,uint32_t now,BatteryMeterInfo *);
void battery_model_reset(BatteryModel *);
/* Explicit little-endian, versioned, CRC-protected bounded settings record.
 * A truncated/newer/bad record cannot destroy the other alternating slot. */
size_t battery_profile_encode(const BatteryProfile *,uint8_t *,size_t,uint32_t generation);
bool battery_profile_decode(BatteryProfile *,const uint8_t *,size_t);
bool battery_profile_newer(uint32_t a,uint32_t b);
#endif
