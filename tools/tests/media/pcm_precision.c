#include "audio/audio_decoder.h"
#include "audio/pcm_format.h"
#include "audio/usb_audio_format.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct { FILE *file; uint64_t bytes, cursor; } Input;
static size_t read_input(void *context, void *data, size_t bytes)
{
    Input *in = context;
    size_t got = fread(data, 1, bytes, in->file);
    in->cursor += got;
    return got;
}
static bool seek_input(void *context, uint64_t offset)
{
    Input *in = context;
    if (offset > in->bytes || fseek(in->file, (long)offset, SEEK_SET)) return false;
    in->cursor = offset;
    return true;
}
static uint64_t tell_input(void *context) { return ((Input *)context)->cursor; }
static uint32_t now(void) { return 0; }
static void arithmetic(void)
{
    const int32_t values[] = {INT32_MIN, INT32_MAX, -1, 0, 1, 0x12345678, -0x12345678};
    for (unsigned i = 0; i < sizeof(values)/sizeof(*values); ++i) {
        int32_t value = values[i];
        assert(audio_pcm_gain(value, 32768) == value);
        assert(audio_pcm_gain(value, 0) == 0);
        assert(audio_pcm_gain(value, 8192) == value / 4);
        uint8_t bytes[4];
        audio_pcm_pack(bytes, value, 4, 32);
        uint32_t packed = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
                          (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
        assert(packed == (uint32_t)value);
    }
    uint8_t bytes[4];
    audio_pcm_pack(bytes, 0x12345600, 3, 24);
    assert(!memcmp(bytes, "\x56\x34\x12", 3));
    audio_pcm_pack(bytes, 0x12345600, 4, 24);
    assert(!memcmp(bytes, "\x00\x56\x34\x12", 4));
    audio_pcm_pack(bytes, 0x12340000, 3, 16);
    assert(!memcmp(bytes, "\x00\x34\x12", 3));
    audio_pcm_pack(bytes, INT32_MAX, 3, 24);
    assert(!memcmp(bytes, "\xff\xff\x7f", 3));
    audio_pcm_pack(bytes, INT32_MIN, 3, 24);
    assert(!memcmp(bytes, "\x00\x00\x80", 3));
    audio_pcm_pack(bytes, -256, 3, 24);
    assert(!memcmp(bytes, "\xff\xff\xff", 3));
    audio_pcm_pack(bytes, INT32_MAX, 2, 16);
    assert(!memcmp(bytes, "\xff\x7f", 2));
    audio_pcm_pack(bytes, INT32_MIN, 2, 16);
    assert(!memcmp(bytes, "\x00\x80", 2));
    /* A single low source bit must survive the appropriate USB subslot. */
    audio_pcm_pack(bytes, 256, 3, 24);
    assert(!memcmp(bytes, "\x01\x00\x00", 3));
    audio_pcm_pack(bytes, 1, 4, 32);
    assert(!memcmp(bytes, "\x01\x00\x00\x00", 4));
    audio_pcm_pack(bytes, 0x12348000, 2, 16);
    assert(!memcmp(bytes, "\x35\x12", 2));
    /* Fractional interpolation must not overflow on opposite full-scale values. */
    int32_t pcm[] = {INT32_MIN, INT32_MAX, INT32_MAX, INT32_MIN};
    AudioBuffer buffer = {.pcm=pcm,.pcm_bits=32,.source_bits=32,.channels=2,.sample_count=2};
    int32_t sample[2];
    audio_buffer_channels_s32(&buffer, 0x80000000ULL, sample);
    assert(sample[0] == -1 && sample[1] == 0 && !buffer.failed);
    int16_t small[] = {-32768, 32767, 1, -1};
    buffer = (AudioBuffer){.pcm=small,.channels=2,.sample_count=2};
    audio_buffer_channels_s32(&buffer, 0, sample);
    assert(sample[0] == INT32_MIN && sample[1] == 2147418112);
    audio_buffer_channels_s32(&buffer, 1ULL << 32, sample);
    assert(sample[0] == 65536 && sample[1] == -65536);
    /* Selection preserves source precision; unsupported widths still fall back. */
    UsbAudioFormat formats[] = {{.sample_bits=16,.channels=2},
                               {.sample_bits=32,.channels=2},{.sample_bits=24,.channels=2}};
    usb_audio_rank_precision(formats, 3, 24, 2);
    assert(formats[0].sample_bits == 24 && formats[1].sample_bits == 32);
    usb_audio_rank_precision(formats, 3, 32, 2);
    assert(formats[0].sample_bits == 32 && formats[1].sample_bits == 24);
    usb_audio_rank_precision(formats, 3, 16, 2);
    assert(formats[0].sample_bits == 16);
    usb_audio_rank_precision(formats, 1, 32, 2);
    assert(formats[0].sample_bits == 16);
    UsbAudioFormat channel_formats[] = {{.sample_bits=32,.channels=1},
                                        {.sample_bits=16,.channels=2}};
    usb_audio_rank_precision(channel_formats, 2, 32, 2);
    assert(channel_formats[0].channels == 2);
    usb_audio_rank_precision(channel_formats, 2, 32, 1);
    assert(channel_formats[0].channels == 1 && channel_formats[0].sample_bits == 32);
}
int main(int argc, char **argv)
{
    assert(argc == 5);
    arithmetic();
    FILE *file = fopen(argv[1], "rb"), *reference = fopen(argv[2], "rb");
    assert(file && reference);
    assert(!fseek(file, 0, SEEK_END));
    long bytes = ftell(file);
    assert(bytes > 0 && !fseek(file, 0, SEEK_SET));
    Input in = {file, (uint64_t)bytes, 0};
    AudioInput input = {&in, read_input, seek_input, tell_input, in.bytes};
    AudioBuffer buffer;
    AudioDecodeStats stats;
    char error[160];
    AudioDecoder *decoder = audio_decoder_open(&buffer, input, &stats, now, 4096, error, sizeof(error));
    if (!decoder) fprintf(stderr, "%s: %s\n", argv[1], error);
    assert(decoder);
    unsigned bits = (unsigned)atoi(argv[3]), channels = (unsigned)atoi(argv[4]);
    assert(buffer.source_bits == bits && buffer.pcm_bits == (bits > 16 ? 32U : 16U));
    assert(buffer.channels == channels);
    uint32_t position = 0;
    while (!audio_decoder_eof(decoder)) {
        assert(audio_decoder_step(decoder, (uint64_t)position << 32) > 0);
        while (position < buffer.pcm_end) {
            int32_t sample[2], expected[2];
            assert(fread(expected, 4, channels, reference) == channels);
            audio_buffer_channels_s32(&buffer, (uint64_t)position << 32, sample);
            assert(!buffer.failed && !memcmp(sample, expected, channels * 4));
            ++position;
        }
    }
    assert(position > 8192 && fgetc(reference) == EOF);
    uint32_t targets[] = {0,1,1031,4093,position/2,position-300,0};
    for (unsigned i = 0; i < sizeof(targets)/sizeof(*targets); ++i) {
        uint32_t at = targets[i];
        assert(audio_decoder_seek(decoder, at));
        while (buffer.pcm_end < at + 257)
            assert(audio_decoder_step(decoder, (uint64_t)at << 32) > 0);
        assert(!fseek(reference, (long)((uint64_t)at * channels * 4), SEEK_SET));
        for (unsigned j = 0; j < 257; ++j) {
            int32_t sample[2], expected[2];
            assert(fread(expected, 4, channels, reference) == channels);
            audio_buffer_channels_s32(&buffer, (uint64_t)(at+j) << 32, sample);
            assert(!buffer.failed && !memcmp(sample, expected, channels * 4));
        }
    }
    printf("PASS %u-bit %u-channel PCM: exact low bits, ring wrap and seeks\n", bits, channels);
    audio_decoder_close(decoder);
    audio_buffer_close(&buffer);
    assert(!fclose(file) && !fclose(reference));
    return 0;
}
