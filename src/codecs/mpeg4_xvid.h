#ifndef NDVIDEO_MPEG4_XVID_H
#define NDVIDEO_MPEG4_XVID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "video_frame.h"

bool mpeg4_xvid_global_init(void *sram_base, unsigned int sram_size);
bool mpeg4_xvid_create(void **out_handle, int width, int height);
bool mpeg4_xvid_reset(void **handle, int width, int height);
void mpeg4_xvid_destroy(void *handle);
bool mpeg4_xvid_decode_frame(
    void *handle,
    const uint8_t *data,
    size_t data_size,
    uint16_t *rgb565,
    int width,
    int height,
    bool output,
    bool discontinuity
);
const char *mpeg4_xvid_last_error(void);
/* Borrowed progressive YUV planes; copy/convert before decoding another frame. */
bool mpeg4_xvid_decode_picture(void *handle, const uint8_t *data, size_t bytes,
    int width, int height, bool discontinuity, VideoFrame *picture);
bool mpeg4_xvid_planar_supported(void *handle);
bool mpeg4_xvid_blit_picture_rows(const VideoFrame *picture, uint16_t *pixels,
    size_t pitch, size_t first, size_t rows);

#endif
