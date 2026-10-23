#ifndef NDVIDEO_AUDIO_BUFFER_H
#define NDVIDEO_AUDIO_BUFFER_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef struct {
    void *pcm;
    uint32_t sample_count, sample_rate, pcm_capacity, pcm_begin, pcm_end, pcm_miss_index;
    unsigned channels;
    unsigned pcm_bits, source_bits;
    bool failed, loop, pcm_missed;
} AudioBuffer;
/* Wide PCM is signed, full-scale 32-bit. Legacy/zero-initialized buffers are s16. */
static inline int32_t audio_buffer_sample_s32(const AudioBuffer *s, size_t index)
{
    return s->pcm_bits == 32 ? ((const int32_t *)s->pcm)[index]
                            : (int32_t)((const int16_t *)s->pcm)[index] * 65536;
}
static inline int16_t audio_buffer_sample_s16(const AudioBuffer *s, size_t index)
{
    return s->pcm_bits == 32 ? (int16_t)(((const int32_t *)s->pcm)[index] >> 16)
                            : ((const int16_t *)s->pcm)[index];
}
void audio_buffer_close(AudioBuffer *);
int16_t audio_buffer_channel(AudioBuffer *, uint64_t, unsigned);
void audio_buffer_channels(AudioBuffer *, uint64_t, int16_t[2]);
void audio_buffer_channels_s32(AudioBuffer *, uint64_t, int32_t[2]);
#endif
