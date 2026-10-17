#include "player/music_visual.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    uint32_t random=17;
    for(unsigned n=0;n<2000;++n) {
        random=random*1664525U+1013904223U;uint16_t a=random>>16;
        random=random*1664525U+1013904223U;uint16_t b=random>>16;
        for(unsigned f=0;f<=32;++f) {
            unsigned r=(((a>>11)*(32-f)+(b>>11)*f)>>5);
            unsigned ga=(a>>5)&63,gb=(b>>5)&63;
            unsigned g=(ga*(32-f)+gb*f)>>5;
            unsigned blue=(((a&31)*(32-f)+(b&31)*f)>>5);
            assert(music_pixel_lerp(a,b,f)==(uint16_t)((r<<11)|(g<<5)|blue));
        }
    }
    MusicVisual visual = {0};
    music_visual_analyze(&visual, 0);
    for (unsigned i = 0; i < MUSIC_VIS_BANDS; ++i)
        assert(!visual.bands[i] && !visual.peaks[i]);
    for(unsigned high=0;high<2;++high) {
        memset(&visual,0,sizeof(visual));
        for(unsigned i=0;i<512;++i) {
            int16_t p=(int16_t)(sin(i*2*3.141592653589793*(high?150:2)/512)*24000);
            visual.pcm[i*2]=p/2;visual.pcm[i*2+1]=p;
        }
        music_visual_analyze(&visual,512);
        assert(visual.balance>70 && visual.onset>100);
        if(high)assert(visual.treble>visual.bass+15);
        else assert(visual.bass>visual.treble+15);
        for(unsigned frame=0;frame<100;++frame)music_visual_analyze(&visual,512);
        assert(!visual.onset); /* A constant tone must not keep triggering attacks. */
    }
    memset(&visual,0,sizeof(visual));
    for (unsigned i = 0; i < 512; ++i)
        visual.pcm[2 * i] = visual.pcm[2 * i + 1] = 12000;
    music_visual_analyze(&visual, 512);
    for (unsigned i = 0; i < MUSIC_VIS_BANDS; ++i)
        assert(!visual.bands[i]);
    for (unsigned i = 0; i < 512; ++i)
    {
        int16_t sine = (int16_t)(sin(i * 2 * 3.141592653589793 * 16 / 512) * 24000);
        visual.pcm[2 * i] = sine;
        visual.pcm[2 * i + 1] = -sine;
    }
    music_visual_analyze(&visual, 512);
    assert(visual.level[0] > 170 && visual.level[1] > 170);
    for (unsigned i = 0; i < MUSIC_VIS_BANDS; ++i)
        assert(!visual.bands[i]);
    for (unsigned i = 0; i < 512; ++i)
        visual.pcm[2 * i + 1] = visual.pcm[2 * i];
    music_visual_analyze(&visual, 512);
    unsigned highest = 0;
    for (unsigned i = 1; i < MUSIC_VIS_BANDS; ++i)
        if (visual.bands[i] > visual.bands[highest])
            highest = i;
    assert(highest == 9); /* FFT bins 14..17; no fake animation/no frequency reversal. */
    uint16_t before = visual.bands[9];
    music_visual_analyze(&visual, 0);
    assert(visual.bands[9] < before);
    for (unsigned frame = 0; frame < 120; ++frame)
        music_visual_analyze(&visual, 0);
    for (unsigned i = 0; i < MUSIC_VIS_BANDS; ++i)
        assert(!visual.bands[i] && !visual.peaks[i]);
    puts("PASS PCM-driven fixed-point spectrum, stereo levels, DC rejection, silence and peak "
         "decay");
}
