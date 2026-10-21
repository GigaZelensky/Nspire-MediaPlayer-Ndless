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
typedef uint16_t Uint16;
typedef struct {int x,y;unsigned w,h;} SDL_Rect;
typedef struct {uint16_t p[320*240];} SDL_Surface;
typedef struct {void *white;} Fonts;
static struct {unsigned row_divider,gunmetal;} theme={0x4a49,0x2945};
#define ui_theme() (&theme)
static BatteryMeterInfo reading;
static bool battery_arrived;static uint32_t battery_arrived_at,fake_now=1000;
static bool reading_ready;
static uint32_t monotonic_clock_now_ms(void){return fake_now;}
static char last_label[16];
static bool battery_meter_info_local(BatteryMeterInfo *out){*out=reading;return reading_ready;}
#define battery_meter_info battery_meter_info_local
static int nSDL_GetStringWidth(void *font,const char *s){(void)font;return (int)strlen(s)*6;}
static void fill_rect_rgb565_mix(SDL_Surface *s,const SDL_Rect *r,uint16_t c,uint8_t mix)
{if(!mix)return;for(unsigned y=0;y<r->h;++y)for(unsigned x=0;x<r->w;++x){int px=r->x+x,py=r->y+y;assert(px>=0 && px<320 && py>=0 && py<240);s->p[py*320+px]=c;}}
static void draw_ui_label_tint(SDL_Surface *s,const Fonts *f,int x,int y,const char *t,uint16_t c,uint8_t mix)
{(void)s;(void)f;(void)x;(void)y;(void)c;if(mix)snprintf(last_label,sizeof(last_label),"%s",t);}
static void draw_battery_power_glyph(SDL_Surface *s,int x,int y,uint8_t mix,bool checking)
{(void)checking;SDL_Rect r={x,y,6,8};fill_rect_rgb565_mix(s,&r,UI_COLOR_WHITE,mix);}
'''
fixture+='\nstatic int clamp_int(int v,int a,int b){return v<a?a:v>b?b:v;}\n'+function((root/'src/player/render_primitives.c').read_text(),'rgb565_lerp')+'\n'+function((root/'src/player/render_primitives.c').read_text(),'blend_rgb565')
fixture+='\n'+function(ui,'ui_ease_smoothstep')+'\n'+function(source,'battery_presence_mix')+'\n'+function(source,'battery_ui_animating')+'\n'+function(source,'draw_battery_meter')+'\n'+function(source,'draw_video_battery_badge')+'\n'+function(source,'battery_meter_width')+'\n'+function(ui,'mix_product_u8')
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
 reading.charging=true;reading.percent_tenths=730;draw_battery_meter(&s,&f,20,20,255);assert(!strcmp(last_label,"73%"));
 unsigned previous=255;
 for(unsigned title=0;title<256;++title){unsigned mix=mix_product_u8(255,255-title);assert(mix<=previous && previous-mix<=1);previous=mix;}
 assert(!previous);
 puts("PASS symmetric six-row fill, content-sized 16px badge, charging icon/percentage and continuous title crossfade");}
'''
cc=os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
if not cc:raise SystemExit('Host C compiler required')
with tempfile.TemporaryDirectory(prefix='nmedia-battery-ui-') as tmp:
    file=Path(tmp)/'badge.c';file.write_text(fixture);exe=Path(tmp)/'badge.exe'
    subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'src'),str(file),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
