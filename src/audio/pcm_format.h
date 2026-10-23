#ifndef NMEDIA_PCM_FORMAT_H
#define NMEDIA_PCM_FORMAT_H
#include <stdint.h>
#include <limits.h>

/* Signed full-scale s32 throughout processing; quantize only at the endpoint. */
static inline int32_t audio_pcm_gain(int32_t sample, unsigned gain_q15)
{
    if (gain_q15 >= 32768)
        return sample;
    if (!gain_q15)
        return 0;
    return (int32_t)((int64_t)sample * gain_q15 / 32768);
}
/* Validated endpoint: 2 <= bytes <= 4, 16 <= bits <= 8 * bytes. */
static inline void audio_pcm_pack(uint8_t *dst, int32_t sample, unsigned bytes, unsigned bits)
{
    unsigned shift = 32 - bits;
    int64_t rounded = sample;
    if (shift) {
        rounded += (int64_t)1 << (shift - 1);
        if (rounded > INT32_MAX)
            rounded = INT32_MAX;
    }
    uint32_t value = (uint32_t)rounded;
    if (shift)
        value &= ~(((uint32_t)1 << shift) - 1);
    /* Valid bits are left justified in a 2/3/4-byte USB PCM subslot. */
    value >>= 8 * (4 - bytes);
    for (unsigned i = 0; i < bytes; ++i)
        dst[i] = (uint8_t)(value >> (8 * i));
}
#endif
