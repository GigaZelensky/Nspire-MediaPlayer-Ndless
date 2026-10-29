#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NDMEDIA_BATTERY_SERVICE 0
#define NDMEDIA_BATTERY_METER 0
#include "audio/audio_playback.h"
#include "audio/audio_metadata.h"
#include "player/music_controls.h"
#include "player/music_visual.h"
#include "player/audio_controls.h"
#include "player/screenshot_writer.h"
#define MAX_PATH_LEN 512
#define NDMEDIA_BATTERY_CALIBRATION 0
#define NDMEDIA_BATTERY_GAUGE_TEST 0
#define NDVIDEO_BATTERY_ACQUISITION 0
#define STATUS_BADGE_ANIM_MS 105U
#define STATUS_BADGE_EXIT_ANIM_MS 95U
#define SCREENSHOT_PREVIEW_MS 100U
#define SCREENSHOT_PREVIEW_MAX_W 80
#define SCREENSHOT_PREVIEW_MAX_H 60
#define CAPTURE_IO_ASYNC_SCREENSHOT 0
#define CAPTURE_SCREENSHOT 0
typedef struct SDL_Surface {unsigned marker;} SDL_Surface;
typedef struct {int unused;} Fonts;
typedef struct {bool off,off_fade_active;} DisplayPowerState;
typedef struct {int x,y;bool visible,moved,press_edge,release_edge,down;} PointerState;
typedef struct {bool initialized,target_active;uint8_t current_mix;} UiTransition;
typedef enum {PLAYBACK_MODE_ONCE,PLAYBACK_MODE_REPEAT,PLAYBACK_MODE_AUTO_NEXT,PLAYBACK_MODE_COUNT} PlaybackMode;
/* PRODUCTION_STATE */
typedef struct LoadingProgress {int unused;} LoadingProgress;
typedef struct {AudioPlayback *audio;MusicControls *controls;uint32_t serviced_at;} MusicWallpaperService;
enum {KEY_NSPIRE_ESC,KEY_NSPIRE_SPACE,KEY_NSPIRE_ENTER,KEY_NSPIRE_TAB,
    KEY_NSPIRE_LEFT,KEY_NSPIRE_RIGHT,KEY_NSPIRE_UP,KEY_NSPIRE_DOWN,
    KEY_NSPIRE_2,KEY_NSPIRE_4,KEY_NSPIRE_5,KEY_NSPIRE_6,KEY_NSPIRE_8,
    KEY_NSPIRE_M,KEY_NSPIRE_O,KEY_NSPIRE_SCRATCHPAD,KEY_NSPIRE_I,KEY_NSPIRE_P,
    KEY_NSPIRE_V,KEY_NSPIRE_C,KEY_NSPIRE_S,KEY_NSPIRE_7,KEY_NSPIRE_9,
    KEY_NSPIRE_CTRL,KEY_NSPIRE_SHIFT};
enum {PLAY_MOVIE_RESULT_EXIT,PLAY_MOVIE_RESULT_ERROR,PLAY_MOVIE_RESULT_SCRATCHPAD_EXIT,
    PLAY_MOVIE_RESULT_AUTO_NEXT,PLAY_MOVIE_RESULT_SWITCH_MOVIE,PLAY_MOVIE_RESULT_APP_EXIT};
struct AudioPlayback {int unused;};
static AudioPlayback stream;
static DisplayPowerState g_display_power_state;
static struct {bool using_hw_timer;} g_clock={true};
static bool protected_content_visible;
static unsigned remembered_view;
static PlaybackMode remembered_mode;
static uint32_t now_ms;
static unsigned scenario,renders,captures,completed,preview_frames,live_surfaces,pumps,services;
static unsigned notices,closed,completed_off;
static uint32_t captured_marker[4];
static char captured_directory[MAX_PATH_LEN];
static unsigned queued_id;
static uint32_t queued_at;
static bool result_ready;
static ScreenshotWriteResult queued_result;
static uint8_t *queued_bytes;
static uint32_t monotonic_clock_now_ms(void){return now_ms;}
static uint64_t monotonic_clock_now_ticks(void){return now_ms;}
#define MONOTONIC_TIMER_VALUE_ADDR ((uintptr_t)&now_ms)
static bool playback_capture_active(const void *v){(void)v;return false;}
static void playback_capture_io(const void *v,int k,int c,unsigned b,uint64_t x,uint64_t y){(void)v;(void)k;(void)c;(void)b;(void)x;(void)y;}
static void playback_capture_stage(const void *v,int k,uint64_t x,uint64_t y){(void)v;(void)k;(void)x;(void)y;}
#define debug_tracef(...) ((void)0)
static const char *filename_from_path(const char *p){const char *s=strrchr(p,'/');return s?s+1:p;}
static void strip_filename(char *p){char *s=strrchr(p,'/');if(s)*s=0;else strcpy(p,".");}
static void clear_screenshot_preview(ScreenshotPreviewState *p){if(p->surface){free(p->surface);--live_surfaces;}memset(p,0,sizeof(*p));}
static SDL_Surface *create_scaled_surface_from_surface(SDL_Surface *s,int w,int h){(void)w;(void)h;SDL_Surface *p=malloc(sizeof(*p));assert(p);*p=*s;++live_surfaces;return p;}
static bool screenshot_encode_bmp(SDL_Surface *s,unsigned char **data,size_t *bytes){*bytes=sizeof(*s);*data=malloc(*bytes);assert(*data);memcpy(*data,s,*bytes);return true;}
bool screenshot_writer_has_capacity(void){return !queued_bytes;}
ScreenshotWriteStatus screenshot_writer_submit(const char *directory,uint8_t *bmp,size_t bytes,uint32_t *id){
    assert(!queued_bytes && bytes==sizeof(SDL_Surface) && captures<4);
    snprintf(captured_directory,sizeof(captured_directory),"%s",directory);
    memcpy(captured_marker+captures,bmp,sizeof(unsigned));++captures;
    queued_bytes=bmp;queued_at=now_ms;*id=++queued_id;
    queued_result=(ScreenshotWriteResult){.id=*id,.success=scenario!=5};
    strcpy(queued_result.path,"/nMedia/ndvideo-shot-0001.bmp.tns");return SCREENSHOT_WRITE_ACCEPTED;
}
bool screenshot_writer_poll(ScreenshotWriteResult *r){if(!result_ready)return false;*r=queued_result;result_ready=false;return true;}
static bool save_screenshot_bitmap_in_directory(SDL_Surface *s,const char *d,char *p,size_t n){(void)s;(void)d;(void)p;(void)n;assert(!"Unexpected blocking fallback");return false;}
static void prepare_screenshot_preview(ScreenshotPreviewState *p,SDL_Surface *s,const char *d){(void)p;(void)s;(void)d;assert(!"Unexpected blocking fallback");}
/* PRODUCTION_SCREENSHOTS */
static bool isKeyPressed(unsigned key){
    if(key==KEY_NSPIRE_ESC)return (now_ms>=400 && now_ms<404) || now_ms>=420;
    if(key==KEY_NSPIRE_S)return ((scenario==6 || now_ms>=20) && now_ms<60) || (now_ms>=120 && now_ms<124);
    return false;
}
static bool key_pressed_edge(unsigned key,bool *before){bool down=isKeyPressed(key),edge=down && !*before;*before=down;return edge;}
static bool on_key_pressed_edge(bool *before){bool down=scenario==4 && now_ms==140,edge=down && !*before;*before=down;return edge;}
static bool any_key_pressed(void){return isKeyPressed(KEY_NSPIRE_S);}
static void player_delay_ms(unsigned ms){now_ms+=ms;assert(now_ms<1000);}
static void player_service_writes(const void *v){(void)v;++services;if(queued_bytes && now_ms-queued_at>=(scenario==4?60U:12U)){free(queued_bytes);queued_bytes=NULL;result_ready=true;++completed;if(g_display_power_state.off)++completed_off;}}
static void movie_picker_timing_stop(void){}
static void cleanup_deferred_playback_movie(void){}
static SDL_Surface *capture_screen_surface(SDL_Surface *s){(void)s;return NULL;}
static void SDL_FreeSurface(SDL_Surface *s){free(s);}
static void loading_progress_init(LoadingProgress *l,SDL_Surface *s,SDL_Surface *b,const Fonts *f,const char *t,bool c){(void)l;(void)s;(void)b;(void)f;(void)t;(void)c;}
static void loading_progress_tick(LoadingProgress *l,bool b){(void)l;(void)b;}
static void music_ui_open(MusicUi *ui,const char *p,LoadingProgress *l){(void)p;(void)l;memset(ui,0,sizeof(*ui));ui->view=scenario==1?MUSIC_VIEW_SCOPE:0;}
void audio_metadata_close(AudioMetadata *m){(void)m;}
AudioPlayback *audio_playback_open(const char *p,const AudioPlaybackTrack *t,LoadingProgress *l){(void)p;(void)t;(void)l;return &stream;}
bool audio_playback_info(const AudioPlayback *a,AudioPlaybackInfo *i){(void)a;*i=(AudioPlaybackInfo){.running=true,.rate=44100,.frames=4410000,.position=now_ms*44,.duration_known=true};return true;}
bool audio_playback_cancelled(const AudioPlayback *a){(void)a;return false;}
const char *audio_playback_status(AudioPlayback *a){(void)a;return "";}
void audio_playback_close(AudioPlayback *a){(void)a;++closed;}
static void music_ui_debug(FILE *f,const void *c){(void)f;(void)c;}
void audio_playback_diagnostics(AudioPlayback *a,void (*fn)(FILE *,const void *),const void *c,bool e){(void)a;(void)fn;(void)c;(void)e;}
static void queue_player_message(const char *t,const char *m){(void)t;(void)m;assert(!"Unexpected error");}
static void battery_calibration_start(uint32_t n){(void)n;}
static bool battery_calibration_active(void){return false;}
static void pointer_init(PointerState *p){memset(p,0,sizeof(*p));}
static void pointer_update(PointerState *p){(void)p;}
static void pointer_update_captured(PointerState *p){(void)p;}
static void music_wallpaper_service(void *c){(void)c;}
static void wallpaper_set_service(void (*fn)(void *),void *c){(void)fn;(void)c;}
void audio_controls_reset(void){}
void audio_controls_set_vertical_offset(int y){(void)y;}
bool audio_controls_touchpad_captured(void){return false;}
bool audio_controls_keyboard_captured(void){return false;}
bool audio_controls_update(AudioPlayback *a,const AudioControlInput *i){(void)a;(void)i;return false;}
bool audio_controls_animating(void){return false;}
static void clock_menu_sync_shortcut(void){}
static void display_power_restore_animated(DisplayPowerState *s,uint32_t n){(void)n;s->off=s->off_fade_active=false;}
static void display_power_restore(DisplayPowerState *s,uint32_t n){display_power_restore_animated(s,n);}
static void display_power_note_activity(DisplayPowerState *s,uint32_t n){(void)s;(void)n;}
static void display_power_request_off(DisplayPowerState *s,bool p,uint32_t n){(void)p;(void)n;s->off=true;}
static void display_power_tick_transition(DisplayPowerState *s,uint32_t n){(void)s;(void)n;}
static void display_power_tick_idle(DisplayPowerState *s,SDL_Surface *screen,uint32_t n,bool a,bool p){(void)s;(void)screen;(void)n;(void)a;(void)p;}
void audio_playback_tick(AudioPlayback *a,bool paused,unsigned n,unsigned d){(void)a;(void)n;(void)d;assert(paused==(scenario==7));}
void audio_playback_pump(AudioPlayback *a){(void)a;++pumps;}
static void ui_cycle_theme(void){}
static void music_ui_brightness(MusicUi *u,bool a,bool b,bool c,bool d,uint32_t n){(void)u;(void)a;(void)b;(void)c;(void)d;(void)n;}
static bool night_mode_poll(uint32_t n,bool b){(void)n;(void)b;return false;}
MusicAction music_controls_update(MusicControls *c,const MusicControlInput *i){c->info_open=scenario==2;c->art_open=scenario==3;c->paused=scenario==7;return (MusicAction){i->back?MUSIC_BACK:MUSIC_NONE,0};}
static void ui_transition_begin_press_release(UiTransition *t,uint32_t n){(void)t;(void)n;}
static const char *playback_mode_text(PlaybackMode m){(void)m;return "";}
static void music_ui_status(MusicUi *u,const char *s,uint32_t n){(void)u;(void)s;(void)n;}
static void music_ui_cycle_view(MusicUi *u){(void)u;}
bool audio_playback_seek(AudioPlayback *a,uint32_t sample){(void)a;(void)sample;assert(!"Screenshot sought audio");return true;}
void audio_playback_suspend(AudioPlayback *a){(void)a;assert(!"Screenshot suspended audio");}
void audio_playback_checkpoint(AudioPlayback *a,const char *s){(void)a;(void)s;}
static void clock_menu_queue_shortcut(void){}
static void clock_menu_poll(SDL_Surface *s,const Fonts *f,void *m,PointerState *p,bool b,const char *path){(void)s;(void)f;(void)m;(void)p;(void)b;(void)path;}
bool audio_playback_waiting_for_output(const AudioPlayback *a){(void)a;return false;}
static const char *media_audio_format(const char *p){(void)p;return "FLAC";}
void audio_playback_context(AudioPlayback *a,uint32_t p,uint32_t f,const char *c){(void)a;(void)p;(void)f;(void)c;}
bool audio_playback_can_reconnect(const AudioPlayback *a){(void)a;return false;}
static int music_wait_for_audio(SDL_Surface *s,const Fonts *f,const char *p,AudioPlayback **a,AudioPlaybackInfo *i,MusicControls *c,PointerState *pointer,MusicUi *u){(void)s;(void)f;(void)p;(void)a;(void)i;(void)c;(void)pointer;(void)u;assert(!"Unexpected recovery");return 0;}
static void music_ui_update(MusicUi *u,AudioPlayback *a,const AudioPlaybackInfo *i,const MusicControls *c,const PointerState *p,uint32_t n){(void)u;(void)a;(void)i;(void)c;(void)p;(void)n;}
static bool ui_theme_transition_active(void){return false;}
static bool battery_ui_animating(void){return false;}
static void music_ui_render(SDL_Surface *s,const Fonts *f,const char *p,AudioPlayback *a,const AudioPlaybackInfo *i,const MusicControls *c,const PointerState *pointer,MusicUi *u,uint32_t n){
    (void)f;(void)p;(void)a;(void)i;(void)pointer;(void)n;assert(!g_display_power_state.off);++renders;
    s->marker=u->view?11:c->info_open?12:c->art_open?13:10;
    if(u->screenshot.label[0]){++preview_frames;if(!strcmp(u->screenshot.label,"Screenshot failed"))++notices;}
}
static void flush_queued_preferences_save(const char *s){(void)s;}
/* PRODUCTION_MUSIC */
int main(void){
    for(scenario=0;scenario<8;++scenario){
        now_ms=0;renders=captures=completed=preview_frames=live_surfaces=pumps=services=notices=closed=completed_off=0;
        queued_id=0;result_ready=false;queued_bytes=NULL;
        g_display_power_state=(DisplayPowerState){.off=scenario==4};
        SDL_Surface screen={0};Fonts fonts={0};char next[MAX_PATH_LEN]={0};
        const char *path=scenario==1?"usb:/nMedia/Music/song.opus":"/nMedia/Album/song.flac.tns";
        assert(play_audio_file(&screen,&fonts,path,next,sizeof(next))==PLAY_MOVIE_RESULT_EXIT);
        unsigned expected=scenario==4 || scenario==6?1:2;
        assert(captures==expected && completed==expected && !queued_bytes);
        for(unsigned i=0;i<captures;++i)assert(captured_marker[i]==(scenario==1?11:scenario==2?12:scenario==3?13:10));
        assert(!strcmp(captured_directory,scenario==1?"usb:/nMedia/Music":"/nMedia/Album"));
        assert(preview_frames && renders && pumps>20 && services>20 && closed==1 && !live_surfaces);
        if(scenario==5)assert(notices);
        if(scenario==4)assert(completed_off==1);
    }
    puts("PASS music S captures current music/visual/Info/expanded-cover frames; holds do not repeat; async saves and failures update preview; screen-off wake, audio continuity and cleanup preserved");
}
