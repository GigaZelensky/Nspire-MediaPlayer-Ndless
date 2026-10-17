#include "media_config.h"
#if NDMEDIA_WITH_WAV
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
/* FFmpeg WAV IMA encoding reconstructs each magnitude with one multiply and
 * one rounding, rather than rounding the separate shifted terms. Match that
 * predictor to avoid accumulating error before each independent block reset. */
#define DRWAV_IMA_UNSIGNED_DIFF(nibble, step) ((((int)(nibble) & 7) * 2 + 1) * (step) >> 3)
#include "vendor/dr_wav.h"
#endif
#if NDMEDIA_WITH_FLAC
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
#include "vendor/dr_flac.h"
#endif
#if NDMEDIA_WITH_MP3
#define MINIMP3_IMPLEMENTATION
#include "mp3_backend.h"
#endif
