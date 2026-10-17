
#ifndef NDVIDEO_OPUS_CONFIG_H
#define NDVIDEO_OPUS_CONFIG_H
#define OPUS_BUILD 1
#define FIXED_POINT 1
#define DISABLE_FLOAT_API 1
#define VAR_ARRAYS 1
#define PACKAGE_VERSION "1.5.2"
#if defined(__arm__) && defined(__GNUC__)
#define OPUS_ARM_INLINE_ASM 1
#define OPUS_ARM_INLINE_EDSP 1
#define OPUS_ARM_PRESUME_EDSP 1
#define asm __asm__
#endif
#endif
