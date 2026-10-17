#ifndef NDVIDEO_COVER_IMAGE_H
#define NDVIDEO_COVER_IMAGE_H
#include <stddef.h>
#include <stdint.h>
/* Caller owns returned square RGB565 thumbnail. Letterboxes rectangular art. */
uint16_t *cover_image_decode(const void *, size_t, unsigned side);
#endif
