#ifndef NMEDIA_BATTERY_CURVE_H
#define NMEDIA_BATTERY_CURVE_H
#include <stdint.h>
enum { BATTERY_CURVE_EMPTY_MV=3498, BATTERY_CURVE_FULL_MV=4200 };
/* Continuous, monotone default estimate in tenths of a percent. */
uint32_t battery_curve_percent(uint32_t millivolts);
uint32_t battery_curve_usable(uint32_t millivolts,uint32_t reserve_mv);
#endif
