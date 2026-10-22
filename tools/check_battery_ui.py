#!/usr/bin/env python3
"""Exercise actual battery glyph/badge geometry and title fade policy."""
from pathlib import Path
import os,shutil,subprocess,tempfile
from check_audio_controls import function
root=Path(__file__).resolve().parents[1]
source=(root/'src/player/battery_ui.c').read_text()
ui=(root/'src/player/playback_ui.c').read_text()
assert '255-ui_mixes->title_strip' in ui
assert 'if(!ui_mixes || !ui_mixes->title_strip) {\n                SDL_Rect battery_panel' not in ui
fixture=r'''
#include "platform/battery_meter.h"
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define NDMEDIA_BATTERY_METER 1
#define UI_COLOR_WHITE 65535
#define UI_COLOR_BLACK 0
#define UI_COLOR_GUNMETAL 0x2945
#define UI_COLOR_ACCENT_HOT 0xfc00
#define UI_COLOR_ACCENT UI_COLOR_ACCENT_HOT
typedef uint16_t Uint16;typedef int16_t Sint16;
typedef struct {Sint16 x,y;Uint16 w,h;} SDL_Rect;
typedef struct {uint16_t p[320*240];} SDL_Surface;
typedef struct {void *white;} Fonts;
static BatteryMeterInfo reading;
static bool battery_arrived;static uint32_t battery_arrived_at,fake_now=1000;
static bool reading_ready,last_power_usb;
static unsigned power_glyph_calls;
static uint32_t monotonic_clock_now_ms(void){return fake_now;}
static char last_label[16];
static bool battery_meter_info_local(BatteryMeterInfo *out){*out=reading;return reading_ready;}
#define battery_meter_info battery_meter_info_local
static int nSDL_GetStringWidth(void *font,const char *s){(void)font;return (int)strlen(s)*6;}
static void fill_rect_rgb565_mix(SDL_Surface *s,const SDL_Rect *r,uint16_t c,uint8_t mix)
{if(!mix)return;for(unsigned y=0;y<r->h;++y)for(unsigned x=0;x<r->w;++x){int px=r->x+x,py=r->y+y;assert(px>=0 && px<320 && py>=0 && py<240);s->p[py*320+px]=c;}}
static void fill_rect_rgb565(SDL_Surface *s,const SDL_Rect *r,uint16_t c)
{fill_rect_rgb565_mix(s,r,c,255);}
static void draw_ui_label_tint(SDL_Surface *s,const Fonts *f,int x,int y,const char *t,uint16_t c,uint8_t mix)
{(void)f;if(mix){snprintf(last_label,sizeof(last_label),"%s",t);SDL_Rect pixel={x,y,1,1};fill_rect_rgb565_mix(s,&pixel,c,mix);}}
static void draw_battery_power_glyph(SDL_Surface *s,int x,int y,uint8_t mix,bool usb_host)
{++power_glyph_calls;last_power_usb=usb_host;SDL_Rect r={x,y,6,8};fill_rect_rgb565_mix(s,&r,UI_COLOR_WHITE,mix);}
'''
fixture+='\nstatic int clamp_int(int v,int a,int b){return v<a?a:v>b?b:v;}\n'+function((root/'src/player/render_primitives.c').read_text(),'rgb565_lerp')+'\n'+function((root/'src/player/render_primitives.c').read_text(),'blend_rgb565')
render=(root/'src/player/render_primitives.c').read_text()
fixture+='\n'+'\n'.join(function(render,n) for n in ('ui_mix_int','control_outline_color','soft_panel_inset_for_row','draw_soft_glass_panel_mix'))
fixture+=r'''
static SDL_Surface before_fade;
static SDL_Surface *begin_faded_region_draw(SDL_Surface *s,const SDL_Rect *r,uint8_t mix,SDL_Surface **target,SDL_Rect *clip,int *dx,int *dy)
{(void)r;(void)mix;(void)clip;before_fade=*s;*target=s;*dx=*dy=0;return NULL;}
static void end_faded_region_draw(SDL_Surface *s,SDL_Surface *layer,const SDL_Rect *r,const SDL_Rect *clip,uint8_t mix)
{(void)layer;(void)clip;if(mix==255)return;for(unsigned y=0;y<r->h;++y)for(unsigned x=0;x<r->w;++x){unsigned at=(r->y+y)*320+r->x+x;s->p[at]=rgb565_lerp(before_fade.p[at],s->p[at],mix,255);}}
'''
fixture+='\n'+function(ui,'offset_sdl_rect')+'\n'+function(ui,'ui_ease_smoothstep')+'\n'+function(source,'battery_presence_mix')+'\n'+function(source,'battery_ui_animating')+'\n'+function(source,'draw_battery_meter_contents')+'\n'+function(source,'draw_battery_meter')+'\n'+function(source,'draw_video_battery_badge')+'\n'+function(source,'battery_meter_width')+'\n'+function(ui,'mix_product_u8')
fixture+=r'''
int main(void){static SDL_Surface s;Fonts f={0};
 reading=(BatteryMeterInfo){.percent_valid=true,.percent_tenths=1000};
 assert(battery_ui_animating() && battery_presence_mix()==0);
 unsigned last=0;for(unsigned i=0;i<=220;++i){fake_now=1000+i;unsigned mix=battery_presence_mix();assert(mix>=last);last=mix;}
 assert(last==255 && !battery_ui_animating());
 draw_battery_meter(&s,&f,20,20,255);assert(!strcmp(last_label,"--") && battery_meter_width(&f)==35);
 assert(s.p[21*320+22]!=65535); /* An unknown reading never invents liquid/percent. */
 reading_ready=true;
 draw_battery_meter(&s,&f,20,20,255);
 for(unsigned y=1;y<=6;++y)assert(s.p[(20+y)*320+22]==65535);
 assert(s.p[20*320+22]!=65535 && s.p[27*320+22]!=65535);
 for(unsigned p=0;p<=1000;p+=10){reading.percent_tenths=p;
   int left=draw_video_battery_badge(&s,&f,220,40,255)+6;
   assert(left==220-23-nSDL_GetStringWidth(NULL,last_label)-8);
   assert(220-left<=55);}
 unsigned calls=power_glyph_calls;
 reading.charging=true;reading.percent_tenths=730;draw_battery_meter(&s,&f,20,20,255);assert(!strcmp(last_label,"73%"));
 assert(power_glyph_calls==calls); /* A stale charging flag is not input power. */
 reading.checking=true;draw_battery_meter(&s,&f,20,20,255);assert(power_glyph_calls==calls);reading.checking=false;
 reading.charging=false;reading.external_power=true;
 draw_battery_meter(&s,&f,20,20,255);assert(power_glyph_calls==calls+1 && !last_power_usb);
 reading.external_power=false;reading.usb_host=true;
 draw_battery_meter(&s,&f,20,20,255);assert(power_glyph_calls==calls+2 && last_power_usb);
 reading.usb_host=false;
 static SDL_Surface opaque,rim;
 reading.charging=false;
 for(unsigned i=0;i<320*240;++i)s.p[i]=0x1234;
 int left=draw_video_battery_badge(&s,&f,220,40,255)+6;opaque=s;rim=before_fade;
 SDL_Rect panel={left,40,220-left,16};draw_soft_glass_panel_mix(&rim,&panel,UI_COLOR_GUNMETAL,0);
 for(unsigned y=0;y<16;++y)for(int x=0;x<220-left;++x)
   if(y<3 || y>=14 || x<3 || x>=220-left-3)
     assert(opaque.p[(40+y)*320+left+x]==rim.p[(40+y)*320+left+x]);
 for(unsigned alpha=0;alpha<=255;alpha+=51) {
   for(unsigned i=0;i<320*240;++i)s.p[i]=0x1234;
   draw_video_battery_badge(&s,&f,220,40,alpha);
   for(unsigned i=0;i<320*240;++i)assert(s.p[i]==(alpha==255?opaque.p[i]:rgb565_lerp(0x1234,opaque.p[i],alpha,255)));
 }
 fake_now=1110;unsigned arrival=battery_presence_mix();
 for(unsigned i=0;i<320*240;++i)s.p[i]=0x1234;
 draw_video_battery_badge(&s,&f,220,40,255);
 for(unsigned i=0;i<320*240;++i)assert(s.p[i]==rgb565_lerp(0x1234,opaque.p[i],arrival,255));
 unsigned previous=255;
 for(unsigned title=0;title<256;++title){unsigned mix=mix_product_u8(255,255-title);assert(mix<=previous && previous-mix<=1);previous=mix;}
 assert(!previous);
 puts("PASS symmetric fill, compact 16px shared-glass badge, charging icon, whole-badge arrival/fade and continuous title crossfade");}
'''
cc=os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
if not cc:raise SystemExit('Host C compiler required')
with tempfile.TemporaryDirectory(prefix='nmedia-battery-ui-') as tmp:
    file=Path(tmp)/'badge.c';file.write_text(fixture);exe=Path(tmp)/'badge.exe'
    subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'src'),str(file),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
