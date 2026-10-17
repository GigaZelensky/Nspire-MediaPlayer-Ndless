
#ifndef NDVIDEO_FAAD_CONFIG_H
#define NDVIDEO_FAAD_CONFIG_H
#define FIXED_POINT 1
#define HAVE_INTTYPES_H 1
#define HAVE_MEMCPY 1
#define HAVE_STRING_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define PACKAGE_VERSION "2.11.2-ndvideo-fixed"
#if defined(__arm__) && defined(__GNUC__)
#define asm __asm__
#endif
#endif
