#ifndef NDVIDEO_NVP_AUDIO_H
#define NDVIDEO_NVP_AUDIO_H
#include "nvp_format.h"
#include <stdbool.h>
#include <stddef.h>
#define NVP_AUDIO_FOOTER_BYTES 48U
typedef enum {
    NVP_AUDIO_PCM = 0,
    NVP_AUDIO_IMA = 1,
    NVP_AUDIO_MP3 = 2,
    NVP_AUDIO_FLAC = 3,
    NVP_AUDIO_VORBIS = 4,
    NVP_AUDIO_AAC = 5,
    NVP_AUDIO_OPUS = 6
} NvpAudioCodec;
typedef struct {
    uint64_t video_bytes, offset, bytes;
    uint32_t rate, samples;
    uint16_t channels, codec;
} NvpAudioTrack;
/* v13 has the same wide header/index layout as v12. The final descriptor
 * bounds video/subtitle data separately from its encoded audio payload. */
bool nvp_audio_parse(NvpAudioTrack *, const void *, size_t, uint64_t, const MovieHeader *);
#endif
