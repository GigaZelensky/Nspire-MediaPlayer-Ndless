#include "wallpaper_image.h"
uint16_t wallpaper_darken(uint16_t p)
{
    /* 31.25% black overlay, baked into the cache. Keep the RGB channel ratios;
     * no gray veil, desaturation or per-frame compositing cost. */
    return (uint16_t)((((p>>11)*11/16)<<11)|((((p>>5)&63)*11/16)<<5)|((p&31)*11/16));
}
static int hue(int r,int g,int b)
{
    int max=r>g?r:g;if(b>max)max=b;
    int min=r<g?r:g;if(b<min)min=b;
    int d=max-min;if(!d)return 0;
    int h=max==r?(g-b)*256/d:max==g?512+(b-r)*256/d:1024+(r-g)*256/d;
    return h<0?h+1536:h;
}
uint16_t wallpaper_saturate(uint16_t p)
{
    int r=(p>>11)*255/31,g=((p>>5)&63)*255/63,b=(p&31)*255/31;
    int max=r>g?r:g;if(b>max)max=b;
    int min=r<g?r:g;if(b<min)min=b;
    if(max>224 && max-min<16)return p; /* Neutral glass highlights. */
    /* Anchor the ribbons to Amber's actual orange (255,82,0), rather than
     * averaging toward the source's yellow highlights. Preserve a little
     * material hue variation, within an orange-only range. */
    int h=max-min>6?90+(hue(r,g,b)-90)/8:90;
    if(h<76)h=76;
    if(h>106)h=106;
    int chroma=(max-min)*2+max/5;
    if(chroma<max*4/5)chroma=max*4/5;
    if(chroma>max)chroma=max;
    min=max-chroma;
    g=min+chroma*h/256;
    return (uint16_t)((max>>3)<<11|(g>>2)<<5|(min>>3));
}
uint16_t wallpaper_recolor(uint16_t p,uint16_t accent)
{
    if(accent==0xfa80U)return p; /* Amber, RGB565(255,82,0). */
    int r=(p>>11)*255/31,g=((p>>5)&63)*255/63,b=(p&31)*255/31;
    int max=r>g?r:g;if(b>max)max=b;
    int min=r<g?r:g;if(b<min)min=b;
    if(max-min<=6)return p;
    int ar=(accent>>11)*255/31,ag=((accent>>5)&63)*255/63,ab=(accent&31)*255/31;
    if(ar==ag && ag==ab) {
        unsigned y=(unsigned)(r*77+g*150+b*29)>>8;
        return (uint16_t)((y>>3)<<11|(y>>2)<<5|(y>>3));
    }
    int h=(hue(r,g,b)+hue(ar,ag,ab)-hue(255,80,0)+1536)%1536;
    int up=min+(max-min)*(h&255)/256,down=max-(max-min)*(h&255)/256;
    switch(h/256) {
    case 0:r=max;g=up;b=min;break;
    case 1:r=down;g=max;b=min;break;
    case 2:r=min;g=max;b=up;break;
    case 3:r=min;g=down;b=max;break;
    case 4:r=up;g=min;b=max;break;
    default:r=max;g=min;b=down;break;
    }
    return (uint16_t)((r>>3)<<11|(g>>2)<<5|(b>>3));
}
