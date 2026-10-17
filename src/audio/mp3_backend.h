#ifndef NDVIDEO_MP3_BACKEND_H
#define NDVIDEO_MP3_BACKEND_H
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_NO_STDIO
#include "vendor/minimp3.h"

/* Retain the tested streaming/gapless/index layer. Only its frame synthesis
 * uses libmad, whose integer multiply path suits the ARM926 without an FPU. */
struct Mp3Fixed;
void mp3_fixed_reset(struct Mp3Fixed **state);
int mp3_fixed_decode(struct Mp3Fixed **state, mp3dec_t *parser, const uint8_t *input,
                     int bytes, mp3d_sample_t *pcm, mp3dec_frame_info_t *info);
#define MINIMP3_EX_USER_FIELDS struct Mp3Fixed *fixed;
#define MINIMP3_EX_RESET(dec) mp3_fixed_reset(&(dec)->fixed)
#define MINIMP3_EX_DECODE(dec, data, bytes, pcm, info) \
    mp3_fixed_decode(&(dec)->fixed, &(dec)->mp3d, data, bytes, pcm, info)
#include "vendor/minimp3_ex.h"
#endif
