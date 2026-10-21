#ifndef NMEDIA_BATTERY_METER_H
#define NMEDIA_BATTERY_METER_H
#include "../media_config.h"
#include "battery_model.h"
#include <stdio.h>
#if NDMEDIA_BATTERY_METER
void battery_meter_init(void);
/* Explicit foreground only. Music pumps its audio before and after active
 * acquisition, then passes false here. Never enter from a clock callback. */
void battery_meter_tick(uint32_t now,uint32_t load_key,bool allow_acquisition);
bool battery_meter_info(BatteryMeterInfo *);
void battery_meter_debug(FILE *);
uint32_t battery_meter_load_key(bool video,bool usb,uint32_t codec,uint32_t mhz,
                                uint32_t brightness,bool screen_off,uint32_t volume,
                                uint32_t rate,uint32_t channels,uint32_t topology);
void battery_meter_reset(void);
/* Only at normal startup/shutdown, with playback and USB jobs closed. */
void battery_meter_load(void);
bool battery_meter_save(void);
#else
static inline void battery_meter_init(void){}
static inline void battery_meter_tick(uint32_t n,uint32_t k,bool a){(void)n;(void)k;(void)a;}
static inline bool battery_meter_info(BatteryMeterInfo *o){if(o)*o=(BatteryMeterInfo){0};return false;}
static inline void battery_meter_debug(FILE *f){(void)f;}
static inline uint32_t battery_meter_load_key(bool v,bool u,uint32_t c,uint32_t m,uint32_t b,
 bool s,uint32_t l,uint32_t r,uint32_t h,uint32_t t)
{(void)v;(void)u;(void)c;(void)m;(void)b;(void)s;(void)l;(void)r;(void)h;(void)t;return 0;}
static inline void battery_meter_reset(void){}
static inline void battery_meter_load(void){}
static inline bool battery_meter_save(void){return true;}
#endif
#endif
