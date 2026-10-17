#include "mp3_backend.h"
#include "vendor/libmad/config.h"
#include "vendor/libmad/fixed.h"
#include "vendor/libmad/bit.h"
#include "vendor/libmad/timer.h"
#include "vendor/libmad/stream.h"
#include "vendor/libmad/frame.h"
#include "vendor/libmad/synth.h"
#include <stdlib.h>
#include <string.h>

struct Mp3Fixed {
    struct mad_stream stream;
    struct mad_frame frame;
    struct mad_synth synth;
    uint8_t input[2304 + MAD_BUFFER_GUARD];
};

void mp3_fixed_reset(struct Mp3Fixed **state)
{
    if (!*state)
        return;
    mad_synth_finish(&(*state)->synth);
    mad_frame_finish(&(*state)->frame);
    mad_stream_finish(&(*state)->stream);
    free(*state);
    *state = NULL;
}

static int16_t to_s16(mad_fixed_t value)
{
    /* Round once and saturate, with headroom to avoid overflow at full scale. */
    if (value >= MAD_F_ONE)
        return 32767;
    if (value < -MAD_F_ONE)
        return -32768;
    int rounded = (value + (1 << (MAD_F_FRACBITS - 16))) >> (MAD_F_FRACBITS - 15);
    return rounded > 32767 ? 32767 : (int16_t)rounded;
}

int mp3_fixed_decode(struct Mp3Fixed **state, mp3dec_t *parser, const uint8_t *input,
                     int bytes, mp3d_sample_t *pcm, mp3dec_frame_info_t *info)
{
    /* Header parsing with NULL PCM does no floating-point synthesis. */
    int samples = mp3dec_decode_frame(parser, input, bytes, NULL, info);
    if (!samples || !pcm)
        return samples;
    int size = info->frame_bytes - info->frame_offset;
    if (info->layer != 3 || size <= 0 || size > 2304)
        return -1;
    if (!*state) {
        *state = calloc(1, sizeof(**state));
        if (!*state)
            return -1;
        mad_stream_init(&(*state)->stream);
        mad_frame_init(&(*state)->frame);
        mad_synth_init(&(*state)->synth);
    }
    struct Mp3Fixed *s = *state;
    memcpy(s->input, input + info->frame_offset, (size_t)size);
    memset(s->input + size, 0, MAD_BUFFER_GUARD);
    /* libmad examines the next frame's header/side-info to retain reservoir
     * bytes. Preserve that lookahead; only the true end of input is padded. */
    int lookahead = bytes - info->frame_bytes;
    if (lookahead > MAD_BUFFER_GUARD)
        lookahead = MAD_BUFFER_GUARD;
    if (lookahead > 0)
        memcpy(s->input + size, input + info->frame_bytes, (size_t)lookahead);
    /* The parser has already established the size of a free-format frame. */
    if (!info->bitrate_kbps)
        s->stream.freerate = (unsigned long)(size - ((input[info->frame_offset+2] >> 1) & 1)) *
                            (unsigned)info->hz / (samples == 576 ? 72U : 144U);
    mad_stream_buffer(&s->stream, s->input, (unsigned long)size + MAD_BUFFER_GUARD);
    if (mad_frame_decode(&s->frame, &s->stream)) {
#ifdef NDVIDEO_MP3_TRACE
        fprintf(stderr, "libmad error=%x %s size=%d md=%u\n", s->stream.error,
                mad_stream_errorstr(&s->stream), size, s->stream.md_len);
#endif
        return s->stream.error == MAD_ERROR_BADDATAPTR ? 0 : -1;
    }
    mad_synth_frame(&s->synth, &s->frame);
    if (s->synth.pcm.channels != info->channels || s->synth.pcm.length != samples)
        return -1;
    for (int i = 0; i < samples; ++i)
        for (int ch = 0; ch < info->channels; ++ch)
            pcm[i * info->channels + ch] = to_s16(s->synth.pcm.samples[ch][i]);
    return samples;
}
