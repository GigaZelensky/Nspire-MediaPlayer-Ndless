#include "nvp_audio.h"
#include "nvp_validation.h"
#include <string.h>
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t get64(const uint8_t *p)
{
    return get32(p) | (uint64_t)get32(p + 4) << 32;
}
bool nvp_audio_parse(NvpAudioTrack *out, const void *data, size_t bytes, uint64_t file_bytes,
                     const MovieHeader *header)
{
    if (!out || !data || !header || file_bytes > INT64_MAX ||
        header->version != MOVIE_VERSION_MEDIA || bytes != NVP_AUDIO_FOOTER_BYTES ||
        file_bytes < NVP_AUDIO_FOOTER_BYTES || memcmp(data, "NVPAUD13", 8))
        return false;
    const uint8_t *p = data;
    NvpAudioTrack track = {get64(p + 8),
                           get64(p + 16),
                           get64(p + 24),
                           get32(p + 32),
                           get32(p + 36),
                           (uint16_t)(p[40] | p[41] << 8),
                           (uint16_t)(p[42] | p[43] << 8)};
    uint64_t end = file_bytes - NVP_AUDIO_FOOTER_BYTES;
    if (get32(p + 44) || track.video_bytes != track.offset || track.offset > end ||
        track.bytes != end - track.offset || track.bytes < 12 || track.rate < 8000 ||
        track.rate > 96000 || !track.samples || track.channels < 1 || track.channels > 2 ||
        track.codec > NVP_AUDIO_OPUS || !nvp_header_is_valid(header, track.video_bytes))
        return false;
    /* Stored duration follows the final video frame count, with only sample
     * rounding permitted. Decoder padding stays outside this logical clock. */
    uint64_t clock = (uint64_t)header->frame_count * header->fps_den;
    if (clock > (UINT64_MAX - header->fps_num / 2) / track.rate)
        return false;
    uint64_t expected = (clock * track.rate + header->fps_num / 2) / header->fps_num;
    if (expected != track.samples)
        return false;
    *out = track;
    return true;
}
