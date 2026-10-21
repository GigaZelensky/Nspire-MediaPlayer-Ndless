#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint16_t Uint16;typedef int16_t Sint16;
typedef struct {Sint16 x,y;uint16_t w,h;} SDL_Rect;
typedef struct {int w,h,pitch;uint16_t *pixels;SDL_Rect clip;void *format;} SDL_Surface;
typedef struct {int unused;} Fonts;
#define SCREEN_W 320
#define SCREEN_H 240
#define UI_COLOR_BLACK 0
#define UI_LOADING_DIM_ALPHA 112
#define SDL_MUSTLOCK(s) 0
static int SDL_LockSurface(SDL_Surface *s){(void)s;return 0;}
static void SDL_UnlockSurface(SDL_Surface *s){(void)s;}
static bool surface_is_rgb565(SDL_Surface *s){(void)s;return true;}
static int clamp_int(int n,int a,int b){return n<a?a:n>b?b:n;}
static unsigned map_rgb565(SDL_Surface *s,unsigned c){(void)s;return c;}
static unsigned SDL_MapRGB(void *f,unsigned r,unsigned g,unsigned b){(void)f;return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);}
static void SDL_GetClipRect(SDL_Surface *s,SDL_Rect *r){*r=s->clip;}
static void SDL_SetClipRect(SDL_Surface *s,SDL_Rect *r){s->clip=r?*r:(SDL_Rect){0,0,s->w,s->h};}
static bool inside(SDL_Surface *s,int x,int y)
{return x>=0 && y>=0 && x<s->w && y<s->h && x>=s->clip.x && x<s->clip.x+s->clip.w && y>=s->clip.y && y<s->clip.y+s->clip.h;}
static void SDL_FillRect(SDL_Surface *s,SDL_Rect *r,unsigned c)
{SDL_Rect q=r?*r:s->clip;int x0=clamp_int(q.x,s->clip.x,s->clip.x+s->clip.w),y0=clamp_int(q.y,s->clip.y,s->clip.y+s->clip.h),x1=clamp_int(q.x+q.w,s->clip.x,s->clip.x+s->clip.w),y1=clamp_int(q.y+q.h,s->clip.y,s->clip.y+s->clip.h);q=(SDL_Rect){x0,y0,x1-x0,y1-y0};if(r)*r=q;for(int y=q.y;y<q.y+q.h;++y)for(int x=q.x;x<q.x+q.w;++x)if(inside(s,x,y))s->pixels[y*s->pitch/2+x]=(uint16_t)c;}
static void SDL_BlitSurface(SDL_Surface *src,SDL_Rect *source,SDL_Surface *dst,SDL_Rect *dest)
{SDL_Rect a=source?*source:(SDL_Rect){0,0,src->w,src->h};int dx=dest?dest->x:0,dy=dest?dest->y:0;for(int y=0;y<a.h;++y)for(int x=0;x<a.w;++x)if(inside(dst,dx+x,dy+y))dst->pixels[(dy+y)*dst->pitch/2+dx+x]=src->pixels[(a.y+y)*src->pitch/2+a.x+x];if(dest){dest->w=a.w;dest->h=a.h;}}
static void present_screen(SDL_Surface *s){(void)s;}
static void draw_loading_overlay_mix(SDL_Surface *s,const Fonts *f,const char *label,int phase,uint8_t mix,bool opening)
{(void)f;(void)label;(void)phase;(void)mix;(void)opening;SDL_Rect r={88,92,136,30};SDL_FillRect(s,&r,0x39e7);}
/* PRODUCTION_FUNCTIONS */
int main(void)
{
    static uint16_t pixels[324*240],snap[324*240],other[324*240];
    SDL_Surface s={320,240,648,pixels,{0,0,320,240},NULL},snapshot={320,240,648,snap,{0,0,320,240},NULL};
    SDL_Surface source={320,240,648,other,{0,0,320,240},NULL};Fonts f={0};
    for(unsigned i=0;i<324*240;++i){pixels[i]=0x0841;snap[i]=0;other[i]=0xffff;}
    s.clip=(SDL_Rect){88,0,136,240};
    reference_loading(&s,&snapshot,&f,"Loading",255,0,true,true);
    assert(pixels[10*324+10]!=pixels[10*324+100]); /* Reproduces the vertical band. */
    for(unsigned i=0;i<324*240;++i)pixels[i]=0x0841;
    draw_loading_transition_frame(&s,&snapshot,&f,"Loading",255,0,true,true);
    for(int y=0;y<240;++y)for(int x=0;x<320;++x)assert(pixels[y*324+x]==(x>=88 && x<224 && y>=92 && y<122?0x39e7:0));
    assert(s.clip.x==88 && s.clip.w==136); /* Caller clip restored. */
    for(unsigned operation=0;operation<4;++operation) {
        for(unsigned i=0;i<324*240;++i)pixels[i]=0x6b4d;
        s.clip=(SDL_Rect){4,3,7,5};SDL_Rect all={0,0,320,240};
        if(operation==0)fill_rect_rgb565_mix(&s,&all,0xffff,128);
        if(operation==1)draw_vertical_gradient(&s,&all,0xffff,0xffff);
        if(operation==2)dim_rect_rgb565(&s,&all,128);
        if(operation==3)blit_surface_rgb565_mix(&s,&source,&all,128);
        for(int y=0;y<240;++y)for(int x=0;x<324;++x)assert((pixels[y*324+x]!=0x6b4d)==(x>=4 && x<11 && y>=3 && y<8));
    }
    s.clip=(SDL_Rect){0,0,320,240};SDL_Rect line={10,10,20,1};
    SDL_Rect original={-3,-4,80,60},kept=original;
    s.clip=(SDL_Rect){20,20,10,10};fill_rect_rgb565(&s,&original,0xffff);
    assert(!memcmp(&original,&kept,sizeof(original))); /* SDL must not rewrite layout. */
    s.clip=(SDL_Rect){0,0,320,240};
    for(unsigned x=10;x<30;++x)pixels[10*324+x]=(uint16_t)(x*1234);
    uint16_t before[20];memcpy(before,pixels+10*324+10,sizeof(before));
    fill_picker_selection_line(&s,&line,0xfa80,1);
    for(unsigned x=0;x<20;++x)assert(pixels[10*324+10+x]==blend_rgb565(before[x],0xfa80,1));
    puts("PASS loading-band reproduction/fix, full-frame snapshot restore, clip/pitch safety and wallpaper-preserving hover blend");
}
