/* Embedded configuration for the player's libmad subset. */
#ifndef NDVIDEO_MAD_CONFIG_H
#define NDVIDEO_MAD_CONFIG_H
#define HAVE_ASSERT_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define SIZEOF_INT 4
#define SIZEOF_LONG_LONG 8
#if defined(__arm__) && defined(__GNUC__)
#define FPM_ARM
#define asm __asm__
#else
#define FPM_64BIT
#endif
#endif
