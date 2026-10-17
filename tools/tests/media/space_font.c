#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum {SDL_SRCCOLORKEY=1};
typedef struct {unsigned colorkey;} Format;
typedef struct {int x,y;unsigned w,h;} SDL_Rect;
typedef struct {int w,h;unsigned flags;Format *format;uint16_t pixels[64];bool locked;} SDL_Surface;
typedef struct {SDL_Surface *chars[256];} nSDL_Font;
#define SDL_MUSTLOCK(g) true
static int SDL_LockSurface(SDL_Surface *g) {assert(!g->locked);g->locked=true;return 0;}
static void SDL_UnlockSurface(SDL_Surface *g) {assert(g->locked);g->locked=false;}
static bool surface_pixel_has_ink(SDL_Surface *g,int x,int y)
{assert(g->locked);return g->pixels[y*8+x]!=g->format->colorkey;}
static int SDL_FillRect(SDL_Surface *g,const SDL_Rect *r,unsigned color)
{assert(!g->locked);for(unsigned y=r->y;y<r->y+r->h;++y)for(unsigned x=r->x;x<r->x+r->w;++x)g->pixels[y*8+x]=(uint16_t)color;return 0;}
/* PRODUCTION_SPACE_FONT */
int main(void)
{
    static const unsigned char broken[][9]={
        {'-',0x60,0,0,0,0xfc,0,0,0},
        {'q',0x60,0,0,0x7e,0x46,0x42,0x7e,0x02},
        {'r',0x02,0,0,0x7e,0x46,0x60,0x60,0x60},
        {'z',0x7c,0,0,0x7e,0x0c,0x18,0x30,0x7e}
    };
    for(unsigned color=0;color<2;++color)for(unsigned altered=0;altered<2;++altered) {
        nSDL_Font font={0};SDL_Surface glyphs[4];Format format={.colorkey=color?0:1};
        for(unsigned i=0;i<4;++i) {
            glyphs[i]=(SDL_Surface){.w=8,.h=8,.flags=SDL_SRCCOLORKEY,.format=&format};
            font.chars[broken[i][0]]=glyphs+i;
            for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
                glyphs[i].pixels[y*8+x]=(broken[i][y+1]&(128U>>x))?color:format.colorkey;
            if(altered)glyphs[i].pixels[7*8+7]=color;
        }
        SDL_Surface before[4];memcpy(before,glyphs,sizeof(before));
        repair_space_font(&font);
        for(unsigned i=0;i<4;++i)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
            assert(glyphs[i].pixels[y*8+x]==(!altered && !y?format.colorkey:before[i].pixels[y*8+x]));
        memcpy(before,glyphs,sizeof(before));repair_space_font(&font);
        assert(!memcmp(before,glyphs,sizeof(before)));
    }
    repair_space_font(NULL);
    puts("PASS Space glyph repair removes only known isolated pixels, white/outline, modified fonts and repeated loads");
    return 0;
}
