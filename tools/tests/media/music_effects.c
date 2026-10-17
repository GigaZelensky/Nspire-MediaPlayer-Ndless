#include "player/music_visual.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    struct Guarded
    {
        uint32_t before;
        MusicVisual visual;
        uint32_t after;
    } *memory = calloc(1, sizeof(*memory));
    assert(memory);
    memory->before = 0x12345678;
    memory->after = 0x87654321;
    uint32_t hashes[4] = {0};
    for (unsigned effect = 1; effect <= 4; ++effect)
    {
        MusicVisual *v = &memory->visual;
        memset(v, 0, sizeof(*v));
        uint32_t first = 0;
        for (unsigned frame = 0; frame < 70; ++frame)
        {
            for (unsigned i = 0; i < 512; ++i)
            {
                int16_t p = (int16_t)(sin((i + frame * 19) * 0.09) * 21000);
                v->pcm[2 * i] = p;
                v->pcm[2 * i + 1] = p / 2;
            }
            music_visual_analyze(v, 512);
            int16_t copy[1024];
            memcpy(copy, v->pcm, sizeof(copy));
            music_visual_frame(v, effect, UINT32_MAX - 300 + frame * 40);
            assert(!memcmp(copy, v->pcm, sizeof(copy)));
            const uint16_t *pixels = music_visual_pixels(v);
            uint32_t hash = 0;
            for (unsigned i = 0; i < 160 * 120; ++i)
                hash = hash * 33 + pixels[i];
            if (frame == 20)
                first = hash;
            if (frame == 69)
            {
                hashes[effect - 1] = hash;
                assert(hash && hash != first);
            }
            assert(memory->before == 0x12345678 && memory->after == 0x87654321);
        }
        for (unsigned frame = 0; frame < 140; ++frame)
        {
            music_visual_analyze(v, 0);
            music_visual_frame(v, effect, 3000 + frame * 40);
        }
        const uint16_t *pixels = music_visual_pixels(v);
        for (unsigned i = 0; i < 160 * 120; ++i)
            assert(!pixels[i]);
    }
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned j = i + 1; j < 4; ++j)
            assert(hashes[i] != hashes[j]);
    printf("PASS four distinct moving feedback presets, PCM unchanged, silence fade, bounds and "
           "clock wrap; working set %lu bytes\n",
           (unsigned long)sizeof(MusicVisual));
    free(memory);
}
