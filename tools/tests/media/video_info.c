#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "movie/nvp_audio.h"
#include "platform/battery_meter.h"
#include "player/display_text.h"
#define SCREEN_W 320
#define SCREEN_H 240
#define UI_COLOR_WHITE 65535
typedef struct {int x,y,w,h;} SDL_Rect;
typedef struct {int unused;} SDL_Surface;
typedef struct {void *white;} Fonts;
typedef struct {void *file;MovieHeader header;uint64_t video_bytes;NvpAudioTrack audio_track;uint32_t current_frame;MovieCodec codec;bool encrypted;unsigned subtitle_track_count;} Movie;
static char lines[20][300];static unsigned count,size_reads,composites;
static BatteryMeterInfo meter={.percent_valid=true,.voltage_valid=true,.estimated=true,.percent_tenths=730,.millivolts=3810,.os_percent=75};
static void dim_rect_rgb565(SDL_Surface *s,const SDL_Rect *r,int alpha){(void)s;(void)r;assert(alpha>=0 && alpha<=112);}
static SDL_Surface *begin_faded_region_draw(SDL_Surface *s,const SDL_Rect *r,uint8_t mix,SDL_Surface **target,SDL_Rect *clip,int *dx,int *dy)
{(void)mix;assert(r->x>=0 && r->y>=0 && r->x+r->w<=320 && r->y+r->h<=240);*target=s;*clip=*r;*dx=*dy=0;return NULL;}
static SDL_Rect offset_sdl_rect(const SDL_Rect *r,int dx,int dy){SDL_Rect t=*r;t.x+=dx;t.y+=dy;return t;}
static void draw_accent_info_panel(SDL_Surface *s,const SDL_Rect *r){(void)s;(void)r;}
static void copy_fitted_text(void *font,const char *s,char *out,size_t bytes,int width){(void)font;(void)width;snprintf(out,bytes,"%s",s);}
static uint16_t picker_text_color(bool secondary){(void)secondary;return 0x7bef;}
static void draw_ui_label_tint(SDL_Surface *s,const Fonts *f,int x,int y,const char *text,uint16_t color,uint8_t mix)
{(void)s;(void)f;(void)color;(void)mix;assert(x>=12 && x<308 && y>=28 && y+8<=212);assert(count<20);snprintf(lines[count++],300,"%s",text);}
static unsigned performance_clock_current_mhz(void){return 396;}
static uint64_t movie_file_size(void *f){assert(f);++size_reads;return 12345678;}
static uint32_t movie_frame_time_ms(const Movie *m,uint32_t frame){return (uint64_t)frame*1000*m->header.fps_den/m->header.fps_num;}
static uint32_t movie_duration_ms(const Movie *m){return movie_frame_time_ms(m,m->header.frame_count);}
static void format_clock(uint32_t ms,char *out,size_t bytes){snprintf(out,bytes,"%lu:%02lu",(unsigned long)ms/60000,(unsigned long)(ms/1000)%60);}
static void format_file_size(uint64_t n,char *out,size_t bytes){snprintf(out,bytes,"%llu bytes",(unsigned long long)n);}
static bool usb_path(const char *path){return !strncmp(path,"usb:",4);}
static bool battery_meter_info_local(BatteryMeterInfo *out){*out=meter;return true;}
#define battery_meter_info battery_meter_info_local
static void end_faded_region_draw(SDL_Surface *s,SDL_Surface *layer,const SDL_Rect *r,const SDL_Rect *clip,uint8_t mix)
{(void)s;(void)layer;(void)r;(void)clip;(void)mix;++composites;}
/* PRODUCTION_FUNCTIONS */
static bool contains(const char *text){for(unsigned i=0;i<count;++i)if(strstr(lines[i],text))return true;return false;}
int main(void)
{
    bool open=false,esc=false,suppress=false;
    video_info_control(&open,true,&esc,&suppress);assert(open && !esc && !suppress);
    esc=true;video_info_control(&open,false,&esc,&suppress);assert(!open && !esc && suppress);
    suppress=false;esc=true;video_info_control(&open,false,&esc,&suppress);assert(esc && !suppress);
    esc=false;video_info_control(&open,true,&esc,&suppress);assert(open);
    video_info_control(&open,true,&esc,&suppress);assert(!open);
    SDL_Surface s={0};Fonts f={0};Movie m={.file=&s,.codec=MOVIE_CODEC_H264,.current_frame=720,.subtitle_track_count=2,
        .header={.video_width=320,.video_height=180,.canvas_width=320,.canvas_height=240,.fps_num=24,.fps_den=1,.frame_count=31680},
        .audio_track={.codec=NVP_AUDIO_OPUS,.rate=48000,.channels=2,.bytes=1000000}};
    draw_video_info(&s,&f,&m,"usb:/nMedia/clip.nvp",0);assert(!count && !size_reads && !composites);
    draw_video_info(&s,&f,&m,"usb:/nMedia/clip.nvp",255);
    assert(size_reads==1 && composites==1 && contains("H.264 / 320x180 / 24.000 fps"));
    assert(contains("0:30 / 22:00") && contains("Opus / 48000 Hz / 2 ch") && contains("73%") && contains("3.810 V"));
    assert(contains("Source: USB drive") && contains("clip.nvp") && contains("Playback continues"));
    count=0;m.audio_track.bytes=0;meter.charging=true;
    draw_video_info(&s,&f,&m,"/documents/local.nvp.tns",128);
    assert(contains("Audio: none") && contains("charging") && contains("Source: calculator"));
    puts("PASS nonblocking info toggle/Esc consumption, cached video/audio/battery fields, local/USB and visible bounds");
}
