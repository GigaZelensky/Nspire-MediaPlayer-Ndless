#include "player/wallpaper_image.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const unsigned colors[]={0xfa80,0xf81f,0x07ff,0xf800,0x07e0,0x001f,0xffff};
    unsigned hashes[7]={0};
    for(unsigned p=0;p<=65535;++p) {
        assert(wallpaper_recolor((uint16_t)p,0xfa80)==p);
        uint16_t dark=wallpaper_darken((uint16_t)p);
        assert((dark>>11)==(p>>11)*11/16 && ((dark>>5)&63)==((p>>5)&63)*11/16 && (dark&31)==(p&31)*11/16);
        for(unsigned i=0;i<7;++i)hashes[i]=hashes[i]*33+wallpaper_recolor((uint16_t)p,(uint16_t)colors[i]);
    }
    uint16_t vivid=wallpaper_saturate(0x8410);
    assert((vivid>>11)>(vivid&31)*3 && wallpaper_saturate(0xffff)==0xffff && !wallpaper_saturate(0));
    /* Yellow source glass stays orange, close to the UI's Amber accent. */
    unsigned orange=wallpaper_saturate(0xffe0);
    assert(((orange>>5)&63)<=(orange>>11) && ((orange>>5)&63)>=(orange>>11)/2);
    for(unsigned i=0;i<7;++i) {
        assert(wallpaper_recolor(0,colors[i])==0 && wallpaper_recolor(0xffff,colors[i])==0xffff);
        for(unsigned j=i+1;j<7;++j)assert(hashes[i]!=hashes[j]);
    }
    puts("PASS theme hue mapping, exact Amber identity, neutral highlights and distinct palettes across every RGB565 input");
}
