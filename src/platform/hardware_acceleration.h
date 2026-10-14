#ifndef NDVIDEO_HARDWARE_ACCELERATION_H
#define NDVIDEO_HARDWARE_ACCELERATION_H
#include <stdint.h>
enum { HW_ACCEL_QUAD=1U, HW_ACCEL_USB_LARGE=2U, HW_ACCEL_SHA=8U };
#if defined(_TINSPIRE)
extern volatile uint32_t hardware_acceleration_mask;
static inline int hardware_acceleration_enabled(unsigned feature)
{return (hardware_acceleration_mask&feature)!=0;}
#else
static inline int hardware_acceleration_enabled(unsigned feature){(void)feature;return 1;}
#endif
#endif
