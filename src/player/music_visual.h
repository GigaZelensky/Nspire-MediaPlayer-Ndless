#ifndef NDVIDEO_MUSIC_VISUAL_H
#define NDVIDEO_MUSIC_VISUAL_H
#include <stdint.h>
#include <stddef.h>
enum
{
    MUSIC_VIS_SAMPLES = 512,
    MUSIC_VIS_BANDS = 24,
    MUSIC_VIS_WIDTH = 160,
    MUSIC_VIS_HEIGHT = 120
};
typedef struct
{
    int16_t pcm[MUSIC_VIS_SAMPLES * 2];
    int32_t real[MUSIC_VIS_SAMPLES], imag[MUSIC_VIS_SAMPLES];
    uint16_t bands[MUSIC_VIS_BANDS], peaks[MUSIC_VIS_BANDS], level[2];
    uint16_t bass,mid,treble,onset,energy_mean;
    int16_t balance;
    int16_t wave[96];
    uint16_t pixels[2][MUSIC_VIS_WIDTH * MUSIC_VIS_HEIGHT];
    uint32_t phase, frame_ms;
    unsigned front, effect;
} MusicVisual;
void music_visual_analyze(MusicVisual *, size_t frames);
int music_visual_sine(unsigned phase);
uint16_t music_pixel_lerp(uint16_t a,uint16_t b,unsigned fraction);
void music_visual_frame(MusicVisual *, unsigned effect, uint32_t now_ms);
const uint16_t *music_visual_pixels(const MusicVisual *);
#endif
