#include "music_ui.h"
#include "media_files.h"
#include "usb_browser.h"
#include "storage/usb/usb_storage.h"
#include "player_data.h"
#include "native_interrupts.h"
#include "raw_player_io.h"

typedef struct {
    SDL_Surface *screen,*snapshot;
    const Fonts *fonts;
    PointerState *pointer;
    UiTransition hover,press;
    bool cancelled,captured,keyboard_capture,previous_key;
} MusicReconnectView;

static bool recovery_cancelled(void *context)
{ return ((MusicReconnectView *)context)->cancelled; }

static void recovery_draw(void *context,uint32_t now)
{
    MusicReconnectView *view=context;
    SDL_Surface *screen=view->screen;
    PointerState *pointer=view->pointer;
    if(view->captured && !view->keyboard_capture)pointer_update_captured(pointer);
    else pointer_update(pointer);
    SDL_Rect panel={30,60,260,128},button={124,154,72,23};
    bool key=isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_RET) ||
        isKeyPressed(KEY_NSPIRE_5);
    bool hot=pointer->visible && rect_contains_point(&button,pointer->x,pointer->y);
    if(isKeyPressed(KEY_NSPIRE_ESC))view->cancelled=true;
    if(!view->captured && (pointer->press_edge || (key && !view->previous_key)) && hot) {
        view->captured=true;
        view->keyboard_capture=!pointer->press_edge;
    }
    if(view->captured && !(view->keyboard_capture?key:pointer->down)) {
        if(hot)view->cancelled=true;
        view->captured=false;
        ui_transition_begin_press_release(&view->press,now);
    }
    view->previous_key=key;
    if(view->snapshot)SDL_BlitSurface(view->snapshot,NULL,screen,NULL);
    else {
        SDL_Rect full={0,0,SCREEN_W,SCREEN_H};
        draw_vertical_gradient(screen,&full,UI_COLOR_BG_TOP,UI_COLOR_BG_BOTTOM);
    }
    draw_overlay_backdrop_dim(screen,255);
    draw_soft_glass_panel_body_from_y(screen,&panel,panel.y,UI_COLOR_GUNMETAL);
    SDL_Rect accent={panel.x+2,panel.y,panel.w-4,1};
    fill_rect_rgb565(screen,&accent,UI_COLOR_ACCENT);
    const char *lines[]={"Audio disconnected","Waiting for USB audio","Reconnect to resume playback"};
    const int rows[]={73,100,116};
    for(unsigned i=0;i<3;++i)
        draw_ui_label(screen,view->fonts,(SCREEN_W-nSDL_GetStringWidth(view->fonts->white,lines[i]))/2,
            rows[i],lines[i]);
    uint8_t hover=ui_transition_update(&view->hover,hot,now,UI_HOVER_ANIM_MS);
    uint8_t press=ui_transition_update_press_ex(&view->press,view->captured,now,
        UI_PRESS_ANIM_MS,PICKER_PRESS_RELEASE_ANIM_MS);
    draw_prompt_button(screen,view->fonts,&button,"Cancel",hover,press);
    if(pointer->visible)draw_cursor(screen,pointer->x,pointer->y);
    present_screen(screen);
}

static void save_recovery(const MusicUi *ui,int result)
{
    /* One terminal error record covers attempts which never opened a decoder.
     * A successful recovery is recorded by the ordinary eventual audio close. */
    if(result>0)return;
    usb_browser_release_io();
    raw_player_before_native();
    unsigned mask=native_critical_enter();
    char path[MAX_PATH_LEN];
    if(player_data_prepare() && player_data_path("ndmedia-timing.log.tns",path,sizeof(path))) {
        FILE *file=player_error_log_open(path,true);
        if(file) {
            fprintf(file,"music_recovery_end result=%d attempts=%lu wait_ms=%lu sample=%lu\n",result,
                (unsigned long)ui->reconnect_attempts,(unsigned long)ui->reconnect_max_ms,
                (unsigned long)ui->reconnect_sample);
            usb_storage_debug(file);
            usb_browser_debug(file);
            fclose(file);
        }
    }
    native_critical_leave(mask);
}

/* The decoder/file is reopened after a transport loss: a reinserted volume
 * has a new generation and must not reuse an old FatFs handle. The cover and
 * music controls remain owned by the caller throughout this wait. */
int music_wait_for_audio(SDL_Surface *screen,const Fonts *fonts,const char *path,
                         AudioPlayback **audio,AudioPlaybackInfo *info,MusicControls *controls,
                         PointerState *pointer,MusicUi *ui)
{
    uint32_t resume=info->position,rate=info->rate,started=monotonic_clock_now_ms();
    uint32_t retry_at=started+500,backoff=500;
    uint32_t present_since=0;
    int result=0;
    controls->capture=0;
    controls->scrubbing=controls->pending_seek=false;
    ++ui->reconnects;ui->reconnect_sample=resume;
    display_power_restore(&g_display_power_state,started);
    memset(ui->hover,0,sizeof(ui->hover));memset(&ui->queue,0,sizeof(ui->queue));ui->hovered=0;
    PointerState hidden=*pointer;hidden.visible=false;
    music_ui_render(screen,fonts,path,*audio,info,controls,&hidden,ui,started);
    SDL_Surface *snapshot=capture_screen_surface(screen);
    LoadingProgress loading;
    loading_progress_init(&loading,screen,snapshot,fonts,"Reconnecting audio",true);
    MusicReconnectView view={.screen=screen,.snapshot=snapshot,.fonts=fonts,.pointer=pointer,
        .previous_key=isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_RET) || isKeyPressed(KEY_NSPIRE_5)};
    loading.render=recovery_draw;loading.cancelled=recovery_cancelled;loading.context=&view;
    /* Save the original failure once, before retry objects/cleanup can replace
     * its evidence. Failed background attempts close quietly until cancellation. */
    audio_playback_close(*audio);*audio=NULL;
    for(;;) {
        uint32_t now=monotonic_clock_now_ms();
        if(isKeyPressed(KEY_NSPIRE_ESC))break;
        loading_progress_tick(&loading,false);
        if(view.cancelled)break;
        if((int32_t)(now-retry_at)<0) {player_delay_ms(16);continue;}
        if(!usb_browser_device_present()) {
            (void)usb_storage_mounted(); /* Poll and invalidate the old volume. */
            usb_browser_idle_discovery(screen,now);
        }
        /* A stopped/suspended native host can report no root-port device
         * until its guarded start/resume runs. Waiting for PORT_CONNECT
         * before attempting that wake strands a reinserted hub. The OTG ID
         * signal is independent of that controller/port state. */
        if(!usb_browser_adapter_present()) {
            present_since=0;
            retry_at=monotonic_clock_now_ms()+250;
            continue;
        }
        if(!present_since)present_since=now;
        if(now-present_since<250U){player_delay_ms(16);continue;}
        audio_playback_discard_retry(*audio);*audio=NULL;
        ++ui->reconnect_attempts;
        bool source_ready=!usb_path(path) || usb_storage_mounted();
        if(!source_ready) {
            int connected=usb_browser_retry(screen,fonts,pointer,&loading);
            if(connected<0)break;
            source_ready=connected>0;
        }
        if(isKeyPressed(KEY_NSPIRE_ESC) || view.cancelled)break;
        if(source_ready) {
            *audio=audio_playback_open(path,NULL,&loading);
            if(!*audio) {result=-1;break;}
            if(*audio)audio_playback_diagnostics(*audio,music_ui_debug,ui,ui->cover_error);
            AudioPlaybackInfo fresh={0};
            if(*audio && audio_playback_info(*audio,&fresh) && !fresh.failed) {
                uint64_t target=rate?(uint64_t)resume*fresh.rate/rate:resume;
                if(fresh.duration_known && target>fresh.frames)target=fresh.frames;
                if(target>UINT32_MAX)target=UINT32_MAX;
                if(audio_playback_seek(*audio,(uint32_t)target))
                    audio_playback_tick_loading(*audio,&loading,controls->paused,1,1);
                else {
                    result=-1;break;
                }
                audio_playback_info(*audio,&fresh);
                if(isKeyPressed(KEY_NSPIRE_ESC) || view.cancelled || audio_playback_cancelled(*audio))break;
                if(!fresh.failed) {
                    *info=fresh;
                    ui->reconnect_sample=fresh.position;
                    result=1;break;
                }
            }
            if(isKeyPressed(KEY_NSPIRE_ESC) || view.cancelled || audio_playback_cancelled(*audio))break;
            if(*audio && !audio_playback_can_reconnect(*audio)) {result=-1;break;}
            audio_playback_suspend(*audio);
        }
        now=monotonic_clock_now_ms();
        retry_at=now+backoff;
        if(backoff<4000)backoff*=2;
        loading.last_draw_ms=0;
    }
    uint32_t elapsed=monotonic_clock_now_ms()-started;
    if(elapsed>ui->reconnect_max_ms)ui->reconnect_max_ms=elapsed;
    save_recovery(ui,result);
    if(snapshot)SDL_FreeSurface(snapshot);
    pointer_init(pointer);
    return result;
}
