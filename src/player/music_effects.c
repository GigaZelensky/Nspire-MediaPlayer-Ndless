#include "music_visual.h"
#include <string.h>

/* Software feedback at half LCD resolution. Each preset transforms the previous
 * frame and adds audio-driven light; no random frames or second audio decoder. */
enum
{
    W = MUSIC_VIS_WIDTH,
    H = MUSIC_VIS_HEIGHT
};
static int clamp(int x, int lo, int hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}
static int sn(unsigned p)
{
    return music_visual_sine(p);
}
static int cs(unsigned p)
{
    return sn(p + 128);
}
static uint16_t color(unsigned phase)
{
    static const uint8_t wheel[8][3] = {{72, 32, 255},  {0, 150, 255},  {0, 255, 210},
                                        {112, 64, 255}, {255, 32, 190}, {255, 92, 32},
                                        {255, 210, 64}, {144, 32, 255}};
    unsigned index = (phase >> 8) & 7, next = (index + 1) & 7, mix = phase & 255;
    unsigned r = (wheel[index][0] * (256 - mix) + wheel[next][0] * mix) >> 8;
    unsigned g = (wheel[index][1] * (256 - mix) + wheel[next][1] * mix) >> 8;
    unsigned b = (wheel[index][2] * (256 - mix) + wheel[next][2] * mix) >> 8;
    return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}
static uint16_t add(uint16_t a, uint16_t b, unsigned alpha)
{
    unsigned r = (a >> 11) + ((b >> 11) * alpha >> 8);
    unsigned g = ((a >> 5) & 63) + (((b >> 5) & 63) * alpha >> 8);
    unsigned blue = (a & 31) + ((b & 31) * alpha >> 8);
    return (uint16_t)((r > 31 ? 31 : r) << 11 | (g > 63 ? 63 : g) << 5 | (blue > 31 ? 31 : blue));
}
static void point(uint16_t *frame, int x, int y, uint16_t c, unsigned alpha)
{
    if (x > 0 && y > 0 && x < W - 1 && y < H - 1)
    {
        size_t at = (size_t)y * W + x;
        frame[at] = add(frame[at], c, alpha);
    }
}
static void glow(uint16_t *frame, int xq, int yq, uint16_t c, unsigned light)
{
    int x = xq >> 8, y = yq >> 8;
    unsigned fx = (unsigned)xq & 255, fy = (unsigned)yq & 255;
    point(frame, x, y, c, light * (256 - fx) * (256 - fy) >> 16);
    point(frame, x + 1, y, c, light * fx * (256 - fy) >> 16);
    point(frame, x, y + 1, c, light * (256 - fx) * fy >> 16);
    point(frame, x + 1, y + 1, c, light * fx * fy >> 16);
    /* Wide colored halo with a narrow, almost white inner filament. */
    point(frame, x - 1, y, c, light / 6);
    point(frame, x + 1, y, c, light / 6);
    point(frame, x, y - 1, c, light / 6);
    point(frame, x, y + 1, c, light / 6);
    point(frame, x, y, 0xffff, light / 7);
}
static void stroke(uint16_t *frame, int x0, int y0, int x1, int y1, uint16_t c, unsigned light)
{
    int dx = x1 - x0, dy = y1 - y0;
    unsigned length = (unsigned)((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx)
                                                                           : (dy < 0 ? -dy : dy)) /
                          192 +
                      1;
    if (length > 80)
        length = 80;
    /* Q16 stepping replaces two software 64-bit divisions per plotted point.
     * All generated coordinates are bounded to a few hundred pixels. Keep the
     * final endpoint exact so adjacent segments cannot grow a gap. */
    int x=x0*256,y=y0*256,step_x=dx*256/(int)length,step_y=dy*256/(int)length;
    for (unsigned i = 1; i < length; ++i) {
        x+=step_x;y+=step_y;
        glow(frame,x>>8,y>>8,c,light);
    }
    glow(frame,x1,y1,c,light);
}
static uint16_t sample(const uint16_t *frame, int xq, int yq)
{
    int x = xq >> 8, y = yq >> 8;
    if (x < 0 || y < 0 || x >= W - 1 || y >= H - 1)
        return 0;
    unsigned fx = (unsigned)xq & 255, fy = (unsigned)yq & 255;
    unsigned weights[4] = {(256 - fx) * (256 - fy), fx * (256 - fy), (256 - fx) * fy, fx * fy};
    uint16_t pixels[4] = {frame[y * W + x], frame[y * W + x + 1], frame[(y + 1) * W + x],
                          frame[(y + 1) * W + x + 1]};
    unsigned r = 0, g = 0, b = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        r += (pixels[i] >> 11) * weights[i];
        g += ((pixels[i] >> 5) & 63) * weights[i];
        b += (pixels[i] & 31) * weights[i];
    }
    return (uint16_t)((r >> 16) << 11 | (g >> 16) << 5 | (b >> 16));
}
static void feedback(MusicVisual *v, unsigned mode)
{
    uint16_t *out = v->pixels[v->front];
    const uint16_t *in = v->pixels[v->front ^ 1];
    int twist = mode == 2   ? 14 + (int)v->treble / 16 + v->onset/24
                : mode == 3 ? -6-(int)v->treble/48
                : mode == 4 ? 0
                            : 5 + sn(v->phase / 8) * 6 / 32768;
    int scale = mode == 4 ? 249-v->onset/48 : mode == 2 ? 251 : 254;
    for (int y = 0; y < H; ++y)
    {
        int yy = y - H / 2;
        int sway = mode == 1 ? sn(v->phase / 2 + y * 3) * 100 / 32768 : 0;
        int sx=-(W/2)*scale-yy*twist+W*128+sway;
        int sy=yy*scale-(W/2)*twist+H*128;
        for (int x = 0; x < W; ++x)
        {
            uint16_t p = sample(in, sx, sy);
            sx+=scale;sy+=twist;
            /* A black floor bounds afterimages and lets silence fade fully. */
            unsigned r = p >> 11, g = (p >> 5) & 63, b = p & 31;
            r = r ? r - 1 : 0;
            g = g > 1 ? g - 2 : 0;
            b = b ? b - 1 : 0;
            out[y * W + x] = (uint16_t)(r << 11 | g << 5 | b);
        }
    }
}
static void ribbons(MusicVisual *v, unsigned mode, unsigned energy)
{
    uint16_t *frame = v->pixels[v->front];
    unsigned phase = v->phase;
    unsigned ribbons = mode == 3 ? 2 : 4;
    for (unsigned ribbon = 0; ribbon < ribbons; ++ribbon)
    {
        int px = 0, py = 0;
        for (unsigned i = 0; i < 96; ++i)
        {
            unsigned t = i * 512 / 95;
            int wave = v->wave[i] * (6+v->mid/24) / 32768;
            int x, y;
            if (mode == 1)
            {
                x = 8 * 256 + (int)i * 144 * 256 / 95;
                y = H * 128 +
                    (sn(t + phase / 2 + ribbon * 27) * (17 + (int)energy / 20) +
                     sn(t * 2 - phase / 3 + ribbon * 53) * (8+v->bass/24)) /
                        128 +
                    wave * 256;
            }
            else
            {
                unsigned a = t + phase / 3 + ribbon * 13;
                int radius = 35 + (int)v->bass / 12 + sn(phase / 7 + t * 3) * 6 / 32768;
                x = W * 128 + sn(a) * radius / 128;
                y = H * 128 + sn(a * 2 + phase / 5) * (20 + cs(phase / 9) * 9 / 32768) / 128 +
                    wave * 128;
            }
            x+=v->balance*6;
            uint16_t c = color(phase / 2 + i * 5 + ribbon * 170+v->treble*2);
            if (i)
            {
                stroke(frame, px, py, x, y, c, mode == 3 ? 80 + energy / 6 : 150 + energy / 3);
                if (mode == 3)
                {
                    stroke(frame, W * 256 - px, py, W * 256 - x, y, c, 70 + energy / 6);
                    stroke(frame, px, H * 256 - py, x, H * 256 - y, c, 70 + energy / 6);
                    stroke(frame, W * 256 - px, H * 256 - py, W * 256 - x, H * 256 - y, c,
                           70 + energy / 6);
                }
            }
            px = x;
            py = y;
        }
    }
}
static void vortex(MusicVisual *v, unsigned energy)
{
    uint16_t *frame = v->pixels[v->front];
    unsigned phase = v->phase;
    for (unsigned arm = 0; arm < 5; ++arm)
    {
        int px = W * 128, py = H * 128;
        for (unsigned i = 0; i < 78; ++i)
        {
            unsigned a = phase + arm * 512 / 5 + i * 4;
            int radius = 5 * 256 + (int)i * (110 + (int)v->bass / 3+v->onset/8);
            int wave = v->wave[i % 96] * 4 / 32768;
            int x = W * 128 + sn(a) * radius / 32768,
                y = H * 128 + cs(a) * radius * 3 / (32768 * 4) + wave * 256;
            stroke(frame, px, py, x, y, color(phase / 2 + arm * 270 + i * 5+v->treble*3), 100 + energy / 2);
            px = x;
            py = y;
        }
    }
}
static void pulse(MusicVisual *v, unsigned energy)
{
    uint16_t *frame = v->pixels[v->front];
    unsigned phase = v->phase;
    int px = 0, py = 0;
    for (unsigned i = 0; i <= 144; ++i)
    {
        unsigned a = i * 512 / 144, band = (i % 144) * MUSIC_VIS_BANDS / 144;
        int wave = v->wave[i % 96] * 7 / 32768;
        int radius = (20 + (int)v->bass / 14 + (int)v->bands[band] / 18 + v->onset/32+wave) * 256;
        int x = W * 128 + sn(a + phase / 7) * radius / 32768;
        int y = H * 128 + cs(a + phase / 7) * radius / 32768;
        if (i)
            stroke(frame, px, py, x, y, color(phase / 2 + i * 11), 150 + energy / 3);
        if (!(i % 6))
        {
            int outer = radius + v->peaks[band] * 16;
            stroke(frame, x, y, W * 128 + sn(a + phase / 7) * outer / 32768,
                   H * 128 + cs(a + phase / 7) * outer / 32768, color(phase / 2 + i * 11 + 220),
                   130 + energy / 3);
        }
        px = x;
        py = y;
    }
}
void music_visual_frame(MusicVisual *v, unsigned effect, uint32_t now)
{
    if (effect < 1 || effect > 4)
        return;
    if (v->effect != effect)
    {
        memset(v->pixels, 0, sizeof(v->pixels));
        v->effect = effect;
        v->frame_ms = now;
    }
    unsigned elapsed = v->frame_ms ? now - v->frame_ms : 40;
    v->frame_ms = now;
    elapsed = (unsigned)clamp((int)elapsed, 1, 100);
    v->phase += elapsed / 8 + 1+v->onset/64+v->treble/96;
    v->front ^= 1;
    unsigned energy = (v->level[0] + v->level[1]) / 2;
    feedback(v, effect);
    for (unsigned i = 0; i < 96; ++i)
    {
        int sum = 0;
        unsigned index = i * (MUSIC_VIS_SAMPLES - 5) / 95;
        for (unsigned j = 0; j < 5; ++j)
            sum += ((int)v->pcm[(index + j) * 2] + v->pcm[(index + j) * 2 + 1]) / 2;
        v->wave[i] = (int16_t)((v->wave[i] * 2 + sum / 5) / 3);
    }
    if (energy < 2)
        return;
    if (effect == 2)
        vortex(v, energy);
    else if (effect == 4)
        pulse(v, energy);
    else
        ribbons(v, effect, energy);
}
const uint16_t *music_visual_pixels(const MusicVisual *v)
{
    return v->pixels[v->front];
}
