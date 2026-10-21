#ifndef NMEDIA_NATIVE_USB_TIMING_H
#define NMEDIA_NATIVE_USB_TIMING_H
#include "../platform/portable_resolver.h"
#include <stdio.h>
/* Enabled by the build for USB storage/audio. Standalone resolver consumers
 * keep the read-only path unless they explicitly select this feature. */
#ifndef NDMEDIA_USB_FAST_ENUMERATION
#define NDMEDIA_USB_FAST_ENUMERATION 0
#endif

#if NDMEDIA_USB_FAST_ENUMERATION
typedef struct {void (*write)(uint32_t,uint32_t);void (*sync)(uint32_t,uint32_t);} NativeUsbTimingIo;
bool native_usb_timing_begin(const PortableView *,uint32_t,uint32_t,NativeUsbTimingIo);
bool native_usb_timing_end(void);
uint32_t native_usb_timing_original(const PortableView *,uint32_t,uint32_t);
void native_usb_timing_debug(FILE *);
#else
static inline bool native_usb_timing_end(void){return true;}
static inline uint32_t native_usb_timing_original(const PortableView *v,uint32_t at,uint32_t op)
{(void)v;(void)at;return op;}
static inline void native_usb_timing_debug(FILE *f){(void)f;}
#endif
#endif
