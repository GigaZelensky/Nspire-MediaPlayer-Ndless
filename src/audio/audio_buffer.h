#ifndef NDVIDEO_AUDIO_BUFFER_H
#define NDVIDEO_AUDIO_BUFFER_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef struct {
    int16_t *pcm;
    uint32_t sample_count, sample_rate, pcm_capacity, pcm_begin, pcm_end, pcm_miss_index;
    unsigned channels;
    bool failed, loop, pcm_missed;
} AudioBuffer;
void audio_buffer_close(AudioBuffer *);
int16_t audio_buffer_channel(AudioBuffer *, uint64_t, unsigned);
void audio_buffer_channels(AudioBuffer *, uint64_t, int16_t[2]);
#endif
