#include "player/music_visual.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
void reference_analyze(MusicVisual *,size_t);
static unsigned random_state=0x78b7413;
static unsigned next_random(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
int main(void)
{
    MusicVisual *a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));assert(a && b);
    unsigned compared=0;
    for(unsigned effect=1;effect<=6;++effect)for(unsigned seed=0;seed<8;++seed) {
        memset(a,0,sizeof(*a));memset(b,0,sizeof(*b));
        for(unsigned step=0;step<32;++step) {
            unsigned group=step/4;size_t frames=group==3?0:group==6?17:group==7?513:512;
            for(unsigned i=0;i<MUSIC_VIS_SAMPLES*2;++i) {
                int16_t sample=group==1 || group==7?0:group==2 || group==6?(i%3?(int16_t)(i&1?1:-1):0):(int16_t)next_random();
                if(group==5)sample=i?0:INT16_MIN;
                a->pcm[i]=b->pcm[i]=sample;
            }
            reference_analyze(a,frames);music_visual_analyze(b,frames);
            assert(!memcmp(a,b,sizeof(*a)));
            music_visual_frame(a,effect,100+step*40);music_visual_frame(b,effect,100+step*40);
            assert(!memcmp(a,b,sizeof(*a)));++compared;
        }
    }
    memset(a,0,sizeof(*a));memset(b,0,sizeof(*b));
    const unsigned iterations=20000;
    clock_t start=clock();
    for(unsigned i=0;i<iterations;++i)reference_analyze(a,0);
    clock_t baseline=clock()-start;start=clock();
    for(unsigned i=0;i<iterations;++i)music_visual_analyze(b,0);
    clock_t fast=clock()-start;
    assert(!memcmp(a,b,sizeof(*a)));
    printf("PASS %u all-preset whole-state/feedback comparisons: loud/silence/tiny/nonzero/paused/resumed/clipped/partial/clamped input\n",compared);
    printf("HOST_BENCH iterations=%u reference_ms=%.3f candidate_ms=%.3f speedup=%.3f\n",iterations,
        1000.0*baseline/CLOCKS_PER_SEC,1000.0*fast/CLOCKS_PER_SEC,fast?(double)baseline/fast:0.0);
    free(a);free(b);return 0;
}
