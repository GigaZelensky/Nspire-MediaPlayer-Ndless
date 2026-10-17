#include "movie/nvp_audio.h"
#include "movie/nvp_validation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put32(unsigned char *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (unsigned char)(n >> (i * 8));
}
static void put64(unsigned char *p, uint64_t n)
{
    put32(p, (uint32_t)n);
    put32(p + 4, (uint32_t)(n >> 32));
}

int main(void)
{
    MovieHeader h = {0};
    memcpy(h.magic, "NVP1", 4);
    h.version = 13;
    h.flags = 1;
    h.canvas_width = h.video_width = 320;
    h.canvas_height = h.video_height = 240;
    h.fps_num = 24000;
    h.fps_den = 1000;
    h.frame_count = 240;
    h.chunk_count = 1;
    h.index_offset = 1024;
    unsigned char footer[48] = {0}, copy[48];
    memcpy(footer, "NVPAUD13", 8);
    put64(footer + 8, 1052);
    put64(footer + 16, 1052);
    put64(footer + 24, 10000);
    put32(footer + 32, 44100);
    put32(footer + 36, 441000);
    footer[40] = 2;
    footer[42] = 1;
    NvpAudioTrack track;
    assert(nvp_audio_parse(&track, footer, 48, 11100, &h));
    assert(track.offset == 1052 && track.samples == 441000);
    assert(!nvp_audio_parse(&track, footer, 47, 11100, &h));
    assert(!nvp_audio_parse(&track, footer, 48, 11099, &h));
    assert(!nvp_audio_parse(&track, footer, 48, 11101, &h));
    assert(!nvp_audio_parse(&track, footer, 48, UINT64_MAX, &h));
    /* Every descriptor field has a rejection case; malformed sections must
     * not escape into the video/subtitle range or overflow a file position. */
    const unsigned offsets[] = {0, 8, 16, 24, 32, 36, 40, 42, 44};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        memcpy(copy, footer, 48);
        copy[offsets[i]] ^= 0x80;
        assert(!nvp_audio_parse(&track, copy, 48, 11100, &h));
    }
    h.fps_num = 0;
    assert(!nvp_audio_parse(&track, footer, 48, 11100, &h));
    h.fps_num = 24000;
    h.version = 12;
    assert(!nvp_audio_parse(&track, footer, 48, 11100, &h));
    h.version = 13;
    h.index_offset = 1ULL << 36;
    uint64_t video = h.index_offset + 28;
    put64(footer + 8, video);
    put64(footer + 16, video);
    assert(nvp_audio_parse(&track, footer, 48, video + 10048, &h));
    /* Subtitles are bounded by video_bytes, never the attached soundtrack. */
    h.subtitle_count = 1;
    h.subtitle_offset = video;
    assert(!nvp_audio_parse(&track, footer, 48, video + 10048, &h));
    puts("PASS v13 footer bounds, corrupt descriptors, duration and wide offsets");
    return 0;
}
