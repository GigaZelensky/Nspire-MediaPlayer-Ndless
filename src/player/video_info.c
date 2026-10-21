#include "player_internal.h"
#include "platform/battery_meter.h"
#include "performance_clock.h"
#include "storage/usb/usb_storage.h"

void video_info_control(bool *open,bool toggle,bool *esc_edge,bool *esc_suppressed)
{
    if(*open && *esc_edge) {
        *open=false;*esc_edge=false;*esc_suppressed=true;
    } else if(toggle)*open=!*open;
}
static void info_line(SDL_Surface *s,const Fonts *f,int x,int y,const char *text,bool secondary)
{
    char fitted[256];copy_fitted_text(f->white,text,fitted,sizeof(fitted),278);
    draw_ui_label_tint(s,f,x,y,fitted,secondary?picker_text_color(true):UI_COLOR_WHITE,255);
}
void draw_video_info(SDL_Surface *screen,const Fonts *fonts,const Movie *movie,const char *path,uint8_t mix)
{
    if(!mix)return;
    SDL_Rect full={0,0,SCREEN_W,SCREEN_H},panel={12,28,296,184},clip;
    panel.y+=(255-mix)*5/255;
    SDL_Rect shadow={panel.x+2,panel.y+3,panel.w,panel.h};
    dim_rect_rgb565(screen,&full,(112U*mix+127)/255);
    dim_rect_rgb565(screen,&shadow,(100U*mix+127)/255);
    SDL_Surface *target;int dx,dy;
    SDL_Surface *layer=begin_faded_region_draw(screen,&panel,mix,&target,&clip,&dx,&dy);
    SDL_Rect r=offset_sdl_rect(&panel,dx,dy);draw_accent_info_panel(target,&r);
    const char *name=path?strrchr(path,'/'):NULL;name=name?name+1:path?path:"Video";
    char text[256],size[32],position[24],duration[24],audio_size[32];
    int x=r.x+9,y=r.y+10;
    display_text_ascii(name,text,sizeof(text),path && usb_path(path));
    info_line(target,fonts,x,y,text,false);y+=18;
    const char *codec=movie->codec==MOVIE_CODEC_H264?"H.264":movie->codec==MOVIE_CODEC_HEVC?"HEVC":
        movie->codec==MOVIE_CODEC_AV1?"AV1":movie->codec==MOVIE_CODEC_MPEG4?"MPEG-4":"Video";
    unsigned den=movie->header.fps_den?movie->header.fps_den:1;
    snprintf(text,sizeof(text),"%s / %ux%u / %u.%03u fps",codec,movie->header.video_width,
        movie->header.video_height,movie->header.fps_num/den,(movie->header.fps_num%den)*1000/den);
    info_line(target,fonts,x,y,text,false);y+=14;
    snprintf(text,sizeof(text),"Canvas %ux%u / %u MHz%s",movie->header.canvas_width,
        movie->header.canvas_height,performance_clock_current_mhz(),movie->encrypted?" / encrypted":"");
    info_line(target,fonts,x,y,text,true);y+=14;
    format_file_size(movie_file_size(movie->file),size,sizeof(size));
    format_clock(movie_frame_time_ms(movie,movie->current_frame),position,sizeof(position));
    format_clock(movie_duration_ms(movie),duration,sizeof(duration));
    snprintf(text,sizeof(text),"%s / %s / %s",position,duration,size);
    info_line(target,fonts,x,y,text,true);y+=14;
    static const char *const audio_codecs[]={"PCM","IMA-ADPCM","MP3","FLAC","Vorbis","AAC","Opus"};
    const NvpAudioTrack *audio=&movie->audio_track;
    if(audio->bytes)snprintf(text,sizeof(text),"Audio: %s / %lu Hz / %u ch",
        audio->codec<7?audio_codecs[audio->codec]:"Unknown",(unsigned long)audio->rate,audio->channels);
    else snprintf(text,sizeof(text),"Audio: none");
    info_line(target,fonts,x,y,text,true);y+=14;
    format_file_size(audio->bytes,audio_size,sizeof(audio_size));
    snprintf(text,sizeof(text),"Audio %s / %u subtitle tracks",audio_size,movie->subtitle_track_count);
    info_line(target,fonts,x,y,text,true);y+=14;
    BatteryMeterInfo battery;
    if(battery_meter_info(&battery) && battery.percent_valid) {
        snprintf(text,sizeof(text),"Battery %u%%%s",(unsigned)(battery.percent_tenths+5)/10,
            battery.charging?" / charging":battery.checking?" / checking power":battery.estimated?" / estimated":" / OS level");
        info_line(target,fonts,x,y,text,true);y+=14;
        if(battery.voltage_valid)snprintf(text,sizeof(text),"%lu.%03lu V / OS %lu%%",
            (unsigned long)battery.millivolts/1000,(unsigned long)battery.millivolts%1000,(unsigned long)battery.os_percent);
        else snprintf(text,sizeof(text),"Fresh battery voltage not available");
    } else {snprintf(text,sizeof(text),"Battery reading not available");info_line(target,fonts,x,y,text,true);y+=14;text[0]=0;}
    info_line(target,fonts,x,y,text,true);y+=14;
    snprintf(text,sizeof(text),"Source: %s",path && usb_path(path)?"USB drive":"calculator");
    info_line(target,fonts,x,y,text,true);y+=14;
    display_text_ascii(path?path:"",text,sizeof(text),path && usb_path(path));
    info_line(target,fonts,x,y,text,true);
    info_line(target,fonts,x,r.y+r.h-15,"I / Esc close   Playback continues",true);
    end_faded_region_draw(screen,layer,&panel,&clip,mix);
}
