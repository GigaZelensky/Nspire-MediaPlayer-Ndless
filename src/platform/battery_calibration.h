#ifndef NMEDIA_BATTERY_CALIBRATION_H
#define NMEDIA_BATTERY_CALIBRATION_H
#include "battery_diagnostics.h"
#ifndef NDMEDIA_BATTERY_CALIBRATION
#define NDMEDIA_BATTERY_CALIBRATION 0
#endif
#ifndef NDMEDIA_BATTERY_LOAD_TEST
#define NDMEDIA_BATTERY_LOAD_TEST 0
#endif
#if NDMEDIA_BATTERY_LOAD_TEST && !NDMEDIA_BATTERY_CALIBRATION
#error Battery load comparison requires the calibration recorder
#endif
#if NDMEDIA_BATTERY_CALIBRATION && !NDVIDEO_BATTERY_ACQUISITION
#error Battery calibration requires the separately guarded ADC acquisition build
#endif
enum {
    BATTERY_CAL_PLAYING=1, BATTERY_CAL_PAUSED=2, BATTERY_CAL_SCREEN_OFF=4,
    BATTERY_CAL_USB_SOURCE=8, BATTERY_CAL_VISUAL=16
};
typedef struct {
    uint32_t flags,cpu_mhz,brightness,volume,muted,codec,rate,channels;
} BatteryCalibrationContext;
#if NDMEDIA_BATTERY_CALIBRATION
void battery_calibration_init(void);
void battery_calibration_start(uint32_t now_ms);
void battery_calibration_abort(void);
void battery_calibration_tick(uint32_t now_ms,const BatteryFreshReading *,const BatteryCalibrationContext *);
bool battery_calibration_active(void);
bool battery_calibration_stopped(void);
bool battery_calibration_has_samples(void);
const char *battery_calibration_reason(void);
/* Separate three-minute comparison: off, on at the saved brightness, off.
 * -1 leaves the normal user display policy in control. */
int battery_calibration_screen_target(uint32_t now_ms);
/* Called only after audio, filesystem jobs and USB have closed. One bounded
 * report, no writes during playback and no percentage before curve fitting. */
bool battery_calibration_report(FILE *file);
#else
static inline void battery_calibration_init(void) { }
static inline void battery_calibration_start(uint32_t now) { (void)now; }
static inline void battery_calibration_abort(void) { }
static inline void battery_calibration_tick(uint32_t now,const BatteryFreshReading *r,const BatteryCalibrationContext *c)
{ (void)now;(void)r;(void)c; }
static inline bool battery_calibration_active(void) { return false; }
static inline bool battery_calibration_stopped(void) { return false; }
static inline bool battery_calibration_has_samples(void) { return false; }
static inline const char *battery_calibration_reason(void) { return "disabled"; }
static inline int battery_calibration_screen_target(uint32_t now) { (void)now;return -1; }
static inline bool battery_calibration_report(FILE *file) { (void)file;return false; }
#endif
#endif
