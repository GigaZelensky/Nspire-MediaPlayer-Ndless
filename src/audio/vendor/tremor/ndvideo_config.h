
#ifndef NDVIDEO_TREMOR_CONFIG_H
#define NDVIDEO_TREMOR_CONFIG_H
#if defined(__GNUC__)
#define alloca __builtin_alloca
#endif
#if defined(__arm__) && defined(__GNUC__)
#define _ARM_ASSEM_
#define asm __asm__
#endif
#endif
