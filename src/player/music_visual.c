#include "music_visual.h"
#include <string.h>
uint16_t music_pixel_lerp(uint16_t a,uint16_t b,unsigned fraction)
{
    /* Expand RGB565 into three non-overlapping lanes. At 5-bit precision the
     * complete weighted sum fits in 32 bits, including both packed channels. */
    uint32_t x=((uint32_t)a | ((uint32_t)a<<16))&0x07e0f81fU;
    uint32_t y=((uint32_t)b | ((uint32_t)b<<16))&0x07e0f81fU;
    uint32_t mixed=((x*(32-fraction)+y*fraction)>>5)&0x07e0f81fU;
    return (uint16_t)(mixed | (mixed>>16));
}
static const int16_t quarter[129] = {
    0,     402,   804,   1206,  1608,  2009,  2410,  2811,  3212,  3612,  4011,  4410,  4808,
    5205,  5602,  5998,  6393,  6786,  7179,  7571,  7962,  8351,  8739,  9126,  9512,  9896,
    10278, 10659, 11039, 11417, 11793, 12167, 12539, 12910, 13279, 13645, 14010, 14372, 14732,
    15090, 15446, 15800, 16151, 16499, 16846, 17189, 17530, 17869, 18204, 18537, 18868, 19195,
    19519, 19841, 20159, 20475, 20787, 21096, 21403, 21705, 22005, 22301, 22594, 22884, 23170,
    23452, 23731, 24007, 24279, 24547, 24811, 25072, 25329, 25582, 25832, 26077, 26319, 26556,
    26790, 27019, 27245, 27466, 27683, 27896, 28105, 28310, 28510, 28706, 28898, 29085, 29268,
    29447, 29621, 29791, 29956, 30117, 30273, 30424, 30571, 30714, 30852, 30985, 31113, 31237,
    31356, 31470, 31580, 31685, 31785, 31880, 31971, 32057, 32137, 32213, 32285, 32351, 32412,
    32469, 32521, 32567, 32609, 32646, 32678, 32705, 32728, 32745, 32757, 32765, 32767};
int music_visual_sine(unsigned phase)
{
    phase &= 511;
    unsigned q = phase / 128, x = phase % 128;
    int v = (q & 1) ? quarter[128 - x] : quarter[x];
    return q >= 2 ? -v : v;
}
static unsigned magnitude(int32_t x)
{
    return (unsigned)(x < 0 ? -x : x);
}
static uint16_t logarithmic(unsigned value)
{
    /* dB-like display scale: 6 dB per octave, floor around -66 dBFS. */
    if (!value)
        return 0;
    unsigned bits = 0, v = value;
    while (v >> 1)
    {
        v >>= 1;
        ++bits;
    }
    unsigned base = 1U << bits;
    unsigned result = bits * 20 + (value - base) * 20 / base;
    return result > 255 ? 255 : (uint16_t)result;
}
void music_visual_analyze(MusicVisual *v, size_t frames)
{
    if (frames > MUSIC_VIS_SAMPLES)
        frames = MUSIC_VIS_SAMPLES;
    if (frames < MUSIC_VIS_SAMPLES)
        memset(v->pcm + 2 * frames, 0, (MUSIC_VIS_SAMPLES - frames) * 2 * sizeof(int16_t));
    unsigned peak[2] = {0};
    int32_t mean = 0;
    for (unsigned i = 0; i < MUSIC_VIS_SAMPLES; ++i)
    {
        for (unsigned c = 0; c < 2; ++c)
        {
            unsigned a = magnitude(v->pcm[2 * i + c]);
            if (a > peak[c])
                peak[c] = a;
        }
        mean += ((int32_t)v->pcm[2 * i] + v->pcm[2 * i + 1]) / 2;
    }
    mean /= MUSIC_VIS_SAMPLES;
    for (unsigned c = 0; c < 2; ++c)
    {
        unsigned level = peak[c] * 255 / 32768;
        v->level[c] = level >= v->level[c] ? level : (v->level[c] * 7 + level) / 8;
    }
    unsigned instant=(peak[0]+peak[1])*255U/65536U;
    unsigned attack=instant>v->energy_mean+8U?(instant-v->energy_mean-8U)*3U:0;
    if(attack>255)attack=255;
    v->onset=attack>v->onset?attack:v->onset*3/4;
    v->energy_mean=(v->energy_mean*15+instant+8)/16;
    v->balance=(int16_t)(((int)peak[1]-(int)peak[0])*255/32768);
    for (unsigned i = 0; i < MUSIC_VIS_SAMPLES; ++i)
    {
        unsigned reversed = 0;
        for (unsigned bit = 0; bit < 9; ++bit)
            reversed = (reversed << 1) | ((i >> bit) & 1);
        int32_t sample = ((int32_t)v->pcm[2 * i] + v->pcm[2 * i + 1]) / 2 - mean;
        int window = (32767 - music_visual_sine(i + 128)) / 2;
        /* One bit of headroom also bounds DC-rejected clipped input. */
        v->real[reversed] = (sample * window) >> 16;
        v->imag[reversed] = 0;
    }
    for (unsigned length = 2; length <= 512; length *= 2)
    {
        unsigned half = length / 2, step = 512 / length;
        for (unsigned block = 0; block < 512; block += length)
            for (unsigned j = 0; j < half; ++j)
            {
                int wr = music_visual_sine(j * step + 128), wi = -music_visual_sine(j * step);
                unsigned a = block + j, b = a + half;
                int tr = (v->real[b] * wr - v->imag[b] * wi) >> 15;
                int ti = (v->real[b] * wi + v->imag[b] * wr) >> 15;
                int ar = v->real[a], ai = v->imag[a];
                v->real[a] = (ar + tr) / 2;
                v->imag[a] = (ai + ti) / 2;
                v->real[b] = (ar - tr) / 2;
                v->imag[b] = (ai - ti) / 2;
            }
    }
    static const uint16_t edges[25] = {1,  2,  3,  4,  5,  6,   7,   9,   11,  14,  18,  23, 29,
                                       37, 47, 59, 74, 92, 114, 140, 168, 195, 220, 240, 256};
    for (unsigned band = 0; band < MUSIC_VIS_BANDS; ++band)
    {
        unsigned high = 0;
        for (unsigned bin = edges[band]; bin < edges[band + 1]; ++bin)
        {
            unsigned a = magnitude(v->real[bin]), b = magnitude(v->imag[bin]);
            unsigned m = a > b ? a + b / 2 : b + a / 2;
            if (m > high)
                high = m;
        }
        uint16_t level = logarithmic(high);
        v->bands[band] = level >= v->bands[band] ? level : (v->bands[band] * 3 + level) / 4;
        v->peaks[band] = v->bands[band] >= v->peaks[band] ? v->bands[band]
                         : v->peaks[band] > 3             ? v->peaks[band] - 3
                                                          : 0;
    }
    unsigned bass=0,mid=0,treble=0;
    for(unsigned i=0;i<MUSIC_VIS_BANDS;++i) {
        if(i<6)bass+=v->bands[i];else if(i<16)mid+=v->bands[i];else treble+=v->bands[i];
    }
    v->bass=bass/6;v->mid=mid/10;v->treble=treble/8;
}
