#ifdef NDVIDEO_AUDIO_CONTROLS_TEST
#include "audio_controls_test_platform.h"
#else
#include "player_internal.h"
#endif
#include "audio_controls.h"

enum { AUDIO_HIDE_MS=1800, AUDIO_SLIDE_MS=180, AUDIO_VALUE_MS=90,
       AUDIO_REPEAT_DELAY_MS=350, AUDIO_REPEAT_MS=85,
       AUDIO_PANEL_Y=46, AUDIO_PANEL_H=148, AUDIO_PANEL_W=34,
       AUDIO_PANEL_X=282, AUDIO_EDGE_X=319 };
enum { AUDIO_CAPTURE_NONE, AUDIO_CAPTURE_MUTE,
       AUDIO_CAPTURE_SLIDER, AUDIO_CAPTURE_PANEL };
static struct {
    UiTransition slide, speaker_hover, speaker_press, thumb_hover, thumb_press;
    SDL_Rect panel, speaker, track, thumb;
    uint32_t until_ms, repeat_ms, value_started_ms, revision;
    int capture, repeat_direction, drag_offset;
    int value_q8, from_q8, target_q8;
    int vertical_offset;
    bool initialized, drawn, value_animating, have_until, capture_keyboard;
} controls;

void audio_controls_reset(void) { memset(&controls,0,sizeof(controls)); }
void audio_controls_set_vertical_offset(int offset) { controls.vertical_offset=clamp_int(offset,-16,16); }
bool audio_controls_open(void) { return controls.slide.current_mix!=0; }
bool audio_controls_touchpad_captured(void)
{ return controls.capture!=AUDIO_CAPTURE_NONE && !controls.capture_keyboard; }
bool audio_controls_keyboard_captured(void)
{ return controls.capture!=AUDIO_CAPTURE_NONE && controls.capture_keyboard; }
uint32_t audio_controls_revision(void) { return controls.revision; }
static bool transitioning(const UiTransition *t)
{ return t->current_mix!=(t->target_active?255:0); }
bool audio_controls_animating(void)
{
    return transitioning(&controls.slide) || controls.value_animating ||
        transitioning(&controls.speaker_hover) || transitioning(&controls.speaker_press) ||
        transitioning(&controls.thumb_hover) || transitioning(&controls.thumb_press);
}
static bool contains(const SDL_Rect *r,int x,int y)
{ return x>=r->x && y>=r->y && x<r->x+r->w && y<r->y+r->h; }
static int percent_at(int y)
{
    int offset=clamp_int(controls.track.y+controls.track.h-y,0,controls.track.h);
    return (offset*100+controls.track.h/2)/controls.track.h;
}
static void show(uint32_t now)
{ controls.until_ms=now+AUDIO_HIDE_MS;controls.have_until=true; }
static void change(AudioPlayback *m,int value,uint32_t now)
{
    audio_playback_set_volume(m,(unsigned)clamp_int(value,0,100));
    show(now);
}
bool audio_controls_update(AudioPlayback *movie,const AudioControlInput *in)
{
    AudioPlaybackLevel level;
    bool consumed=controls.capture!=AUDIO_CAPTURE_NONE;
    if (!in->enabled || !audio_playback_level(movie,&level)) {
        bool dirty=controls.drawn || controls.slide.current_mix || controls.capture;
        uint32_t revision=controls.revision;
        audio_controls_reset();
        controls.revision=revision+(dirty?1:0);
        return consumed;
    }
    uint8_t old_mix[5]={controls.slide.current_mix,controls.speaker_hover.current_mix,
        controls.speaker_press.current_mix,controls.thumb_hover.current_mix,controls.thumb_press.current_mix};
    int old_value=controls.value_q8;
    bool live=controls.drawn;
    bool over=live && in->pointer_visible && contains(&controls.panel,in->x,in->y);
    bool speaker_hot=live && in->pointer_visible && contains(&controls.speaker,in->x,in->y);
    bool thumb_hot=over && contains(&controls.thumb,in->x,in->y);
    /* Match the title's edge reveal, but only alongside the volume panel.
     * The timeout bridges the short gap between the edge and sliding panel. */
    bool edge=in->pointer_visible && in->x>=AUDIO_EDGE_X &&
        in->y>=AUDIO_PANEL_Y+controls.vertical_offset && in->y<AUDIO_PANEL_Y+controls.vertical_offset+AUDIO_PANEL_H;
    bool bridge=live && in->pointer_visible && in->x>=controls.panel.x+controls.panel.w &&
        in->x<=AUDIO_EDGE_X && in->y>=AUDIO_PANEL_Y+controls.vertical_offset && in->y<AUDIO_PANEL_Y+controls.vertical_offset+AUDIO_PANEL_H;
    if (edge || bridge) show(in->now_ms);
    if (in->pressed && !controls.capture && !over && (edge || bridge)) {
        controls.capture=AUDIO_CAPTURE_PANEL;
        controls.capture_keyboard=in->keyboard_pressed;
    }
    if (in->pressed && !controls.capture && over) {
        consumed=true;
        controls.capture_keyboard=in->keyboard_pressed;
        if (speaker_hot) controls.capture=AUDIO_CAPTURE_MUTE;
        else if (in->y>=controls.track.y-6 && in->y<=controls.track.y+controls.track.h+6) {
            controls.capture=AUDIO_CAPTURE_SLIDER;
            int center=controls.thumb.y+controls.thumb.h/2;
            controls.drag_offset=thumb_hot?in->y-center:0;
            change(movie,percent_at(in->y-controls.drag_offset),in->now_ms);
        } else controls.capture=AUDIO_CAPTURE_PANEL;
    }
    if (controls.capture==AUDIO_CAPTURE_SLIDER && (in->held || in->released))
        change(movie,percent_at(in->y-controls.drag_offset),in->now_ms);
    if (controls.capture!=AUDIO_CAPTURE_NONE) {
        consumed=true;
        if (in->released || !in->held) {
            if (controls.capture==AUDIO_CAPTURE_MUTE && speaker_hot) audio_playback_mute(movie);
            show(in->now_ms);
            controls.capture=AUDIO_CAPTURE_NONE;
        }
    }
    if (in->mute_pressed) {audio_playback_mute(movie);show(in->now_ms);++controls.revision;}
    int direction=in->up_held==in->down_held?0:in->up_held?1:-1;
    int step=0;
    if (direction && ((direction>0 && in->up_pressed) || (direction<0 && in->down_pressed))) {
        step=direction;controls.repeat_direction=direction;
        controls.repeat_ms=in->now_ms+AUDIO_REPEAT_DELAY_MS;
    } else if (!direction || direction!=controls.repeat_direction) controls.repeat_direction=0;
    else if ((int32_t)(in->now_ms-controls.repeat_ms)>=0) {
        step=direction;controls.repeat_ms=in->now_ms+AUDIO_REPEAT_MS;
    }
    if (step) change(movie,(int)level.volume+step*5,in->now_ms);
    if (controls.drawn && (over || controls.capture)) show(in->now_ms);
    bool visible=(controls.have_until && (int32_t)(controls.until_ms-in->now_ms)>0) ||
        controls.capture!=AUDIO_CAPTURE_NONE;
    ui_transition_update(&controls.slide,visible,in->now_ms,AUDIO_SLIDE_MS);
    ui_transition_update(&controls.speaker_hover,speaker_hot,in->now_ms,UI_HOVER_ANIM_MS);
    ui_transition_update_press_ex(&controls.speaker_press,
        controls.capture==AUDIO_CAPTURE_MUTE,
        in->now_ms,UI_PRESS_ANIM_MS,PICKER_PRESS_RELEASE_ANIM_MS);
    ui_transition_update(&controls.thumb_hover,thumb_hot,in->now_ms,UI_HOVER_ANIM_MS);
    ui_transition_update_press_ex(&controls.thumb_press,controls.capture==AUDIO_CAPTURE_SLIDER,
        in->now_ms,UI_PRESS_ANIM_MS,PICKER_PRESS_RELEASE_ANIM_MS);
    audio_playback_level(movie,&level);
    int target=(level.muted || level.failed?0:(int)level.volume)*256;
    if (!controls.initialized) {
        controls.value_q8=controls.from_q8=controls.target_q8=target;
        controls.initialized=true;
    }
    if (target!=controls.target_q8) {
        controls.from_q8=controls.value_q8;controls.target_q8=target;
        controls.value_started_ms=in->now_ms;controls.value_animating=true;
        ++controls.revision;
    }
    if (controls.value_animating) {
        uint8_t mix=ui_ease_smoothstep(in->now_ms-controls.value_started_ms,AUDIO_VALUE_MS);
        controls.value_q8=controls.from_q8+(controls.target_q8-controls.from_q8)*mix/255;
        if (mix==255) controls.value_animating=false;
    }
    if (old_value!=controls.value_q8 || old_mix[0]!=controls.slide.current_mix ||
        old_mix[1]!=controls.speaker_hover.current_mix || old_mix[2]!=controls.speaker_press.current_mix ||
        old_mix[3]!=controls.thumb_hover.current_mix || old_mix[4]!=controls.thumb_press.current_mix)
        ++controls.revision;
    return consumed;
}
static void icon_line(SDL_Surface *screen,int x,int y,int dx,int dy,Uint16 color)
{
    int length=abs(dx)>abs(dy)?abs(dx):abs(dy);
    for (int i=0;i<=length;++i) {
        SDL_Rect dot={x+(length?dx*i/length:0),y+(length?dy*i/length:0),1,1};
        fill_rect_rgb565(screen,&dot,color);
    }
}
static void speaker_icon(SDL_Surface *screen,int x,int y,bool silent,unsigned volume,Uint16 color)
{
    SDL_Rect body={x,y+5,4,6};fill_rect_rgb565(screen,&body,color);
    for (int i=0;i<5;++i) {
        SDL_Rect column={x+3+i,y+4-i,1,8+2*i};fill_rect_rgb565(screen,&column,color);
    }
    if (silent || !volume) {
        icon_line(screen,x+10,y+5,6,6,color);icon_line(screen,x+10,y+11,6,-6,color);
    } else {
        icon_line(screen,x+9,y+6,1,2,color);icon_line(screen,x+10,y+8,-1,2,color);
        icon_line(screen,x+12,y+4,2,4,color);icon_line(screen,x+14,y+8,-2,4,color);
        icon_line(screen,x+15,y+2,3,6,color);icon_line(screen,x+18,y+8,-3,6,color);
    }
}
void audio_controls_draw(SDL_Surface *screen,const Fonts *fonts,const AudioPlayback *movie,bool chrome,uint32_t now)
{
    (void)now;(void)chrome;
    AudioPlaybackLevel level;
    controls.drawn=false;
    if (!audio_playback_level(movie,&level)) return;
    unsigned mix=controls.slide.current_mix;
    if (!mix) return;
    SDL_Rect panel={320+2-((320+2-AUDIO_PANEL_X)*(int)mix+127)/255,
                    AUDIO_PANEL_Y+controls.vertical_offset,AUDIO_PANEL_W,AUDIO_PANEL_H};
    SDL_Rect speaker={panel.x+5,panel.y+4,24,22};
    controls.panel=panel;controls.speaker=speaker;
    controls.drawn=true;
    if (mix) {
        draw_soft_glass_panel_body_from_y(screen,&panel,panel.y,UI_COLOR_GUNMETAL);
        SDL_Rect accent={panel.x+panel.w-1,panel.y+3,1,panel.h-6};
        fill_rect_rgb565(screen,&accent,UI_COLOR_ACCENT);
    }
    draw_prompt_button(screen,fonts,&speaker,"",controls.speaker_hover.current_mix,controls.speaker_press.current_mix);
    int dx=pressed_control_offset_x(controls.speaker_press.current_mix);
    int dy=pressed_control_offset_y(controls.speaker_press.current_mix);
    Uint16 color=level.failed?UI_COLOR_ACCENT:UI_COLOR_WHITE;
    speaker_icon(screen,speaker.x+(speaker.w-20)/2+dx,speaker.y+(speaker.h-16)/2+dy,
                 level.muted || level.failed,level.volume,color);
    SDL_Rect track={panel.x+14,panel.y+35,5,85};
    controls.track=track;
    draw_progress_track(screen,&track,&panel);
    int fill=(controls.value_q8*track.h+12800)/25600;
    SDL_Rect bar={track.x,track.y+track.h-fill,track.w,fill};
    if (bar.h) {
        draw_vertical_gradient(screen,&bar,ui_theme()->progress_fill_top,ui_theme()->progress_fill_bottom);
        SDL_Rect glow={bar.x+1,bar.y,1,bar.h};
        fill_rect_rgb565(screen,&glow,ui_theme()->progress_fill_glow);
    }
    SDL_Rect thumb={track.x-4,track.y+track.h-fill-3,13,7};controls.thumb=thumb;
    draw_prompt_button(screen,fonts,&thumb,"",controls.thumb_hover.current_mix,controls.thumb_press.current_mix);
    draw_soft_glass_panel_rim(screen,&thumb,UI_COLOR_GUNMETAL,255);
    char text[12];
    if (level.failed) snprintf(text,sizeof(text),"OFF");
    else if (level.muted) snprintf(text,sizeof(text),"MUTE");
    else snprintf(text,sizeof(text),"%u%%",level.volume);
    draw_ui_label(screen,fonts,panel.x+(panel.w-nSDL_GetStringWidth(fonts->white,text))/2,
                  panel.y+panel.h-16,text);
}
