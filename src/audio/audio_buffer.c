#include "audio_buffer.h"
#include <stdlib.h>
#include <string.h>
void audio_buffer_close(AudioBuffer *s)
{
    if (s) {
        free(s->pcm);
        memset(s, 0, sizeof(*s));
    }
}
static bool pcm_available(AudioBuffer *s, uint32_t index)
{
    if (!s->pcm_capacity || (index >= s->pcm_begin && index < s->pcm_end))
        return true;
    s->pcm_missed = s->failed = true;
    s->pcm_miss_index = index;
    return false;
}
static uint32_t pcm_slot(const AudioBuffer *s, uint32_t index)
{
    unsigned capacity = s->pcm_capacity;
    if (!capacity)
        return index;
    return !(capacity & (capacity - 1)) ? index & (capacity - 1) : index % capacity;
}
int16_t audio_buffer_channel(AudioBuffer *s, uint64_t position, unsigned channel)
{
    if (!s->pcm)
        return 0;
    if (!s->sample_count || !s->channels || s->failed)
        return 0;
    uint32_t index = (uint32_t)(position >> 32);
    if (s->loop)
        index %= s->sample_count;
    else if (index >= s->sample_count)
        return 0;
    if (!pcm_available(s, index))
        return 0;
    channel = s->channels == 1 ? 0 : s->channels == 2 ? channel & 1 : channel % s->channels;
    int a = audio_buffer_sample_s16(s, (size_t)pcm_slot(s, index) * s->channels + channel);
    if (!(uint32_t)position)
        return (int16_t)a;
    uint32_t next = index + 1;
    if (next == s->sample_count)
        next = s->loop ? 0 : index;
    if (!pcm_available(s, next))
        return 0;
    int b = audio_buffer_sample_s16(s, (size_t)pcm_slot(s, next) * s->channels + channel);
    return (int16_t)(a + (int64_t)(b - a) * (uint32_t)position / 4294967296LL);
}
void audio_buffer_channels(AudioBuffer *s, uint64_t position, int16_t out[2])
{
    out[0] = out[1] = 0;
    /* Equal source/output rates use integer positions. Load the adjacent
     * channels once, with no interpolation or per-channel division. */
    if (s->pcm && !(uint32_t)position) {
        if (!s->sample_count || !s->channels || s->failed)
            return;
        uint32_t index = (uint32_t)(position >> 32);
        if (s->loop)
            index %= s->sample_count;
        else if (index >= s->sample_count)
            return;
        if (!pcm_available(s, index))
            return;
        size_t offset = (size_t)pcm_slot(s, index) * s->channels;
        out[0] = audio_buffer_sample_s16(s, offset);
        out[1] = audio_buffer_sample_s16(s, offset + (s->channels == 1 ? 0 : 1));
        return;
    }
    out[0] = audio_buffer_channel(s, position, 0);
    out[1] = audio_buffer_channel(s, position, 1);
}
static int32_t channel_s32(AudioBuffer *s, uint64_t position, unsigned channel)
{
    if (!s->pcm || !s->sample_count || !s->channels || s->failed)
        return 0;
    uint32_t index = (uint32_t)(position >> 32);
    if (s->loop)
        index %= s->sample_count;
    else if (index >= s->sample_count)
        return 0;
    if (!pcm_available(s, index))
        return 0;
    channel = s->channels == 1 ? 0 : channel % s->channels;
    int32_t a = audio_buffer_sample_s32(s, (size_t)pcm_slot(s, index) * s->channels + channel);
    if (!(uint32_t)position)
        return a;
    uint32_t next = index + 1;
    if (next == s->sample_count)
        next = s->loop ? 0 : index;
    if (!pcm_available(s, next))
        return 0;
    int32_t b = audio_buffer_sample_s32(s, (size_t)pcm_slot(s, next) * s->channels + channel);
    int64_t delta = (int64_t)b - a;
    /* The difference can span UINT32_MAX. An unsigned product avoids signed
     * 64-bit overflow while keeping interpolation between the two endpoints. */
    uint64_t product = (uint64_t)(delta < 0 ? -delta : delta) * (uint32_t)position;
    int64_t step = (int64_t)(product >> 32);
    return (int32_t)((int64_t)a + (delta < 0 ? -step : step));
}
void audio_buffer_channels_s32(AudioBuffer *s, uint64_t position, int32_t out[2])
{
    out[0] = out[1] = 0;
    if (s->pcm && !(uint32_t)position) {
        if (!s->sample_count || !s->channels || s->failed)
            return;
        uint32_t index = (uint32_t)(position >> 32);
        if (s->loop)
            index %= s->sample_count;
        else if (index >= s->sample_count)
            return;
        if (!pcm_available(s, index))
            return;
        size_t offset = (size_t)pcm_slot(s, index) * s->channels;
        out[0] = audio_buffer_sample_s32(s, offset);
        out[1] = audio_buffer_sample_s32(s, offset + (s->channels == 1 ? 0 : 1));
        return;
    }
    out[0] = channel_s32(s, position, 0);
    out[1] = channel_s32(s, position, 1);
}
