#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "audio/audio_playback.h"
typedef struct SDL_Surface {int unused;} SDL_Surface;
typedef struct {void *white;} Fonts;
typedef struct {int x,y;bool visible,press_edge,down;} PointerState;
typedef struct {int unused;} UiTransition;
typedef void (*MessageBackdropDraw)(SDL_Surface *,void *,uint32_t);
typedef struct {int x,y,w,h;} SDL_Rect;
typedef struct LoadingProgress {const char *label;uint32_t last_draw_ms;
    void (*render)(void *,uint32_t);bool (*cancelled)(void *);void *context;} LoadingProgress;
typedef struct {unsigned capture;bool scrubbing,pending_seek,paused;} MusicControls;
typedef struct {uint32_t reconnects,reconnect_attempts,reconnect_max_ms,reconnect_sample;bool cover_error;
    UiTransition hover[12],queue;int hovered;} MusicUi;
struct AudioPlayback {bool live,failed,retry,running,cancelled;uint32_t rate,position;};
#define SCREEN_W 320
#define SCREEN_H 240
#define UI_COLOR_BG_TOP 0
#define UI_COLOR_BG_BOTTOM 1
#define KEY_NSPIRE_ESC 1
#define KEY_NSPIRE_ENTER 2
#define KEY_NSPIRE_RET 3
#define KEY_NSPIRE_5 4
#define UI_COLOR_GUNMETAL 2
#define UI_COLOR_ACCENT 3
#define UI_HOVER_ANIM_MS 100
#define UI_PRESS_ANIM_MS 60
#define PICKER_PRESS_RELEASE_ANIM_MS 80
static int g_display_power_state;
static uint32_t clock_ms,cancel_at;
static unsigned scenario,closes,quiet_closes,opens,source_retries,attempt_failures,frames,discovery;
static bool mounted;
static bool quiet_reconnect;
static LoadingProgress *retry_progress;
static unsigned recovery_saves;
static unsigned native_polls;
static unsigned retry_feedback,session_restart_marks;
static void usb_browser_request_session_restart(void){++session_restart_marks;}
typedef struct {bool cancelled;} UsbConnectWindow;
static bool render_retry(UsbConnectWindow *window);
static void native_critical_enter(void){}
static void usb_fs_cancel_active(void){}
static bool last_connection_cancelled;
static AudioPlayback original,next[8],*current;
static SDL_Surface background;
static bool usb_path(const char *path){return !strncmp(path,"usb:",4);}
static bool usb_storage_mounted(void){return mounted;}
static uint32_t monotonic_clock_now_ms(void){return clock_ms;}
static bool isKeyPressed(unsigned key){return key==KEY_NSPIRE_ESC?(cancel_at && clock_ms>=cancel_at):
    ((scenario==9 && clock_ms>=200 && clock_ms<260) ||
     (scenario==14 && clock_ms>=900 && clock_ms<960)) && key==KEY_NSPIRE_ENTER;}
static void player_delay_ms(unsigned ms){clock_ms+=ms;assert(clock_ms<15000);}
static void display_power_restore(int *state,uint32_t now){(void)state;(void)now;}
static void music_ui_render(SDL_Surface *s,const Fonts *f,const char *p,AudioPlayback *a,
    const AudioPlaybackInfo *i,const MusicControls *c,const PointerState *pointer,MusicUi *ui,uint32_t now)
{(void)s;(void)f;(void)p;(void)a;(void)i;(void)c;(void)pointer;(void)ui;(void)now;++frames;}
static SDL_Surface *capture_screen_surface(SDL_Surface *screen){(void)screen;return &background;}
static void SDL_FreeSurface(SDL_Surface *screen){assert(screen==&background);}
static void SDL_BlitSurface(SDL_Surface *a,void *b,SDL_Surface *c,void *d){(void)a;(void)b;(void)c;(void)d;}
static void draw_vertical_gradient(SDL_Surface *s,const SDL_Rect *r,int a,int b){(void)s;(void)r;(void)a;(void)b;}
static void loading_progress_init(LoadingProgress *l,SDL_Surface *s,SDL_Surface *snapshot,const Fonts *f,const char *text,bool dim)
{(void)s;(void)snapshot;(void)f;(void)dim;*l=(LoadingProgress){.label=text};}
static void loading_progress_tick(LoadingProgress *l,bool force){(void)force;assert(l->render && l->cancelled);l->render(l->context,clock_ms);++frames;}
void audio_playback_close(AudioPlayback *a){if(!a)return;assert(a->live && current==a);a->live=false;current=NULL;++closes;}
void audio_playback_discard_retry(AudioPlayback *a){if(!a)return;assert(a->live && current==a);a->live=false;current=NULL;++quiet_closes;}
bool audio_playback_can_reconnect(const AudioPlayback *a){return a && a->retry;}
bool audio_playback_cancelled(const AudioPlayback *a){return a && a->cancelled;}
AudioPlayback *audio_playback_open(const char *path,const AudioPlaybackTrack *track,LoadingProgress *loading)
{
    assert(!current && !track && opens<8 && loading && (!usb_path(path)||mounted));
    AudioPlayback *a=next+opens++;*a=(AudioPlayback){.live=true,.rate=48000};current=a;clock_ms+=100;
    if(scenario==3){a->failed=true;a->retry=false;}return a;
}
bool audio_playback_info(const AudioPlayback *a,AudioPlaybackInfo *info)
{if(!a)return false;*info=(AudioPlaybackInfo){a->rate,1000000,a->position,2,3,true,a->running,a->failed,false};return true;}
bool audio_playback_seek(AudioPlayback *a,uint32_t sample){assert(a && a->live);a->position=sample;return true;}
void audio_playback_tick_loading(AudioPlayback *a,LoadingProgress *l,bool paused,unsigned num,unsigned den)
{assert(a && l && num==1 && den==1);clock_ms+=200;
 if(scenario==7){a->failed=a->cancelled=true;}
 else if(scenario==13 && native_polls<6){a->failed=a->retry=true;}
 else if(attempt_failures){--attempt_failures;a->failed=a->retry=true;}else a->running=!paused;}
void audio_playback_suspend(AudioPlayback *a){if(a)a->running=false;}
void audio_playback_diagnostics(AudioPlayback *a,void (*fn)(FILE *,const void *),const void *context,bool error)
{(void)a;(void)fn;(void)context;(void)error;}
static void music_ui_debug(FILE *file,const void *context){(void)file;(void)context;}
static bool usb_browser_device_present(void){
 if(scenario==10)return opens!=0;
 if(scenario==11)return source_retries>=2;
 if(scenario==12)return false;
 return scenario!=2 || clock_ms>=700;
}
static bool usb_browser_adapter_present(void){return scenario!=12;}
static void usb_browser_idle_discovery(SDL_Surface *s,uint32_t now){(void)s;(void)now;++discovery;}
static bool usb_browser_recovery_poll(SDL_Surface *s){(void)s;++native_polls;return true;}
static bool usb_browser_connect(SDL_Surface *s,const Fonts *f,PointerState *p,
    void (*backdrop)(SDL_Surface *,void *,uint32_t),void *context)
{(void)s;(void)f;(void)p;(void)context;assert(!current && quiet_reconnect && retry_progress && !backdrop);
 UsbConnectWindow window={0};assert(render_retry(&window));++source_retries;clock_ms+=100;
 if(scenario>=14 && source_retries==1) {
  for(unsigned i=0;i<30 && !window.cancelled;++i){clock_ms+=16;render_retry(&window);}
  assert(window.cancelled);
 }
 if(window.cancelled){last_connection_cancelled=true;return false;}
 if(scenario==6){last_connection_cancelled=true;return false;}mounted=source_retries>=2;return mounted;}
static void pointer_update(PointerState *p){p->press_edge=scenario==8 && clock_ms>=200 && !p->down && clock_ms<250;
 if(scenario==15)p->press_edge=clock_ms>=900 && !p->down && clock_ms<950;
 p->down=scenario==8 && clock_ms>=200 && clock_ms<250;
 if(scenario==15)p->down=clock_ms>=900 && clock_ms<950;
 if(scenario==8 || scenario==9 || scenario==15){p->visible=true;p->x=scenario==15?116:204;p->y=164;}}
static void pointer_update_captured(PointerState *p){pointer_update(p);p->press_edge=false;}
static void pointer_init(PointerState *p){(void)p;}
static bool rect_contains_point(const SDL_Rect *r,int x,int y){return x>=r->x && x<r->x+r->w && y>=r->y && y<r->y+r->h;}
static void draw_overlay_backdrop_dim(SDL_Surface *s,unsigned m){(void)s;(void)m;}
static void draw_soft_glass_panel_body_from_y(SDL_Surface *s,const SDL_Rect *r,int y,int c){(void)s;(void)r;(void)y;(void)c;}
static void fill_rect_rgb565(SDL_Surface *s,const SDL_Rect *r,int c){(void)s;(void)r;(void)c;}
static int nSDL_GetStringWidth(void *font,const char *s){(void)font;return (int)strlen(s)*6;}
static void draw_ui_label(SDL_Surface *s,const Fonts *f,int x,int y,const char *text){(void)s;(void)f;(void)y;assert(x>=30 && x+(int)strlen(text)*6<=290);}
static unsigned ui_transition_update(UiTransition *t,bool active,uint32_t now,unsigned ms){(void)t;(void)now;(void)ms;return active?255:0;}
static unsigned ui_transition_update_press_ex(UiTransition *t,bool active,uint32_t now,unsigned a,unsigned b){(void)b;return ui_transition_update(t,active,now,a);}
static void ui_transition_begin_press_release(UiTransition *t,uint32_t now){(void)t;(void)now;}
static void draw_prompt_button(SDL_Surface *s,const Fonts *f,const SDL_Rect *r,const char *text,unsigned a,unsigned b){(void)s;(void)f;(void)a;(void)b;assert((!strcmp(text,"Cancel") || !strcmp(text,"Retry") || !strcmp(text,"Retrying")) && r->y==154);if(!strcmp(text,"Retrying"))++retry_feedback;}
static void draw_cursor(SDL_Surface *s,int x,int y){(void)s;(void)x;(void)y;}
static void present_screen(SDL_Surface *s){(void)s;}
static void save_recovery(const MusicUi *ui,int result,AudioPlayback *audio,unsigned manual_retries){(void)audio;assert(ui->reconnect_max_ms==clock_ms);assert(manual_retries==(scenario>=14?1U:0U));if(result<=0)++recovery_saves;}
/* PRODUCTION_RECONNECT */
int main(void)
{
    SDL_Surface screen={0};Fonts fonts={0};PointerState pointer={0};
    for(scenario=0;scenario<16;++scenario){
        clock_ms=closes=quiet_closes=opens=source_retries=frames=discovery=0;
        cancel_at=scenario==1?200:scenario==4 || scenario==12?1000:0;
        recovery_saves=0;pointer=(PointerState){0};retry_progress=NULL;
        native_polls=retry_feedback=session_restart_marks=0;
        mounted=scenario!=2 && scenario!=6 && scenario!=11 && scenario<14;quiet_reconnect=last_connection_cancelled=false;attempt_failures=scenario==0?2:0;
        original=(AudioPlayback){.live=true,.failed=true,.retry=true,.rate=44100,.position=44100};
        current=&original;AudioPlayback *audio=&original;
        AudioPlaybackInfo info={44100,1000000,44100,2,3,true,false,true,false};
        MusicControls controls={.capture=7,.scrubbing=true,.pending_seek=true,.paused=scenario==5};
        MusicUi ui={0};
        int result=music_wait_for_audio(&screen,&fonts,scenario==2 || scenario==6 || scenario==11 || scenario>=14?"usb:/disk/song.flac":"song.flac",
                                      &audio,&info,&controls,&pointer,&ui);
        assert(closes==1 && !controls.capture && !controls.scrubbing && !controls.pending_seek);
        assert(ui.reconnects==1 && ui.reconnect_attempts==opens+source_retries-(scenario==2 || scenario==11 || scenario>=14?1:0));
        if(scenario==1 || scenario==4 || scenario==6 || scenario==7 || scenario==8 || scenario==9 || scenario==12)assert(!result);
        else if(scenario==3)assert(result==-1 && audio && audio->failed);
        else {
            assert(result==1 && info.position==48000 && !info.failed && audio->running==!controls.paused);
            if(scenario==0)assert(opens==3 && quiet_closes==2);
            if(scenario==2)assert(source_retries==2 && discovery);
            if(scenario==10)assert(opens==1 && discovery && !source_retries);
            if(scenario==11)assert(source_retries==2 && discovery);
            if(scenario==13)assert(opens==1 && native_polls>=6 && !discovery);
            if(scenario>=14)assert(source_retries==2 && opens==1 && !recovery_saves && retry_feedback);
        }
        if(scenario==12)assert(!opens && !source_retries && discovery);
        if(audio)audio_playback_close(audio);
        assert(!current && frames && ui.reconnect_max_ms==clock_ms && !quiet_reconnect && !retry_progress);
        assert(recovery_saves==(result<=0));
        assert(session_restart_marks==1U+(scenario>=14?1U:0U));
    }
    puts("PASS music reconnect: adapter-only wake, native child recovery, saved position/rate, quiet failures, remount, pause, cancellation and pointer/Enter retry during USB scan");
    return 0;
}
