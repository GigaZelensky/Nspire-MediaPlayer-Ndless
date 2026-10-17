#define NDVIDEO_AUDIO_CONTROLS_TEST
#include "../../../src/player/audio_controls.c"
int clamp_int(int n,int lo,int hi){return n<lo?lo:n>hi?hi:n;}
int pressed_control_offset_x(uint8_t mix){return mix>128;}
int pressed_control_offset_y(uint8_t mix){return mix>128;}
static const Palette palette={0xfa80,0xd900,0xfd40};
const Palette *ui_theme(void){return &palette;}
void fill_rect_rgb565(SDL_Surface *s,const SDL_Rect *r,Uint16 c)
{
    for(int y=r->y;y<r->y+r->h;++y)for(int x=r->x;x<r->x+r->w;++x)
        if(x>=0 && x<320 && y>=0 && y<240)s->pixels[y*320+x]=c;
}
void draw_soft_glass_panel_body_from_y(SDL_Surface *s,const SDL_Rect *r,int y,Uint16 c)
{(void)y;fill_rect_rgb565(s,r,c);}
void draw_soft_glass_panel_rim(SDL_Surface *s,const SDL_Rect *r,Uint16 c,uint8_t m)
{(void)s;(void)r;(void)c;(void)m;}
void draw_prompt_button(SDL_Surface *s,const Fonts *f,const SDL_Rect *r,const char *t,uint8_t h,uint8_t p)
{(void)f;(void)t;fill_rect_rgb565(s,r,(Uint16)(0x2924+h+p));}
void draw_progress_track(SDL_Surface *s,const SDL_Rect *r,const SDL_Rect *p)
{(void)p;fill_rect_rgb565(s,r,0);}
void draw_vertical_gradient(SDL_Surface *s,const SDL_Rect *r,Uint16 a,Uint16 b)
{(void)b;fill_rect_rgb565(s,r,a);}
void draw_ui_label(SDL_Surface *s,const Fonts *f,int x,int y,const char *t)
{(void)s;(void)f;(void)x;(void)y;(void)t;}
int nSDL_GetStringWidth(void *f,const char *t){(void)f;return (int)strlen(t)*5;}
bool audio_playback_level(const AudioPlayback *m,AudioPlaybackLevel *level)
{if(!m || !m->available)return false;if(level)*level=m->level;return true;}
void audio_playback_set_volume(AudioPlayback *m,unsigned v)
{Movie *mutable=(Movie *)m;assert(v<=100);mutable->level.volume=v;mutable->level.muted=false;}
void audio_playback_mute(AudioPlayback *m)
{Movie *mutable=(Movie *)m;if(m->level.failed)mutable->level.failed=false;else mutable->level.muted=!m->level.muted;}
/* PRODUCTION_TRANSITIONS */
static SDL_Surface surface={.guard_before=0x12345678,.guard_after=0x87654321};
static Fonts fonts;
typedef struct {bool down,press_edge,release_edge;} PointerState;
static struct {bool pressed;int arrow;} g_input_touchpad_report;
static bool g_input_touchpad_sampled=true,g_input_touchpad_ok=true;
static bool pointer_update(PointerState *p)
{
    bool held=g_input_touchpad_report.pressed && !g_input_touchpad_report.arrow;
    p->press_edge=held && !p->down;p->release_edge=!held && p->down;p->down=held;
    return p->press_edge;
}
/* PRODUCTION_CAPTURED_POINTER */
static Movie movie={.available=true,.level={20,false,false}};
static AudioControlInput in={.now_ms=1000,.x=300,.y=200,.enabled=true,.chrome_visible=true,.pointer_visible=true};
static bool tick(unsigned elapsed)
{
    in.now_ms+=elapsed;
    bool captured=audio_controls_update(&movie,&in);
    audio_controls_draw(&surface,&fonts,&movie,in.chrome_visible,in.now_ms);
    assert(surface.guard_before==0x12345678 && surface.guard_after==0x87654321);
    return captured;
}
static void click(int x,int y)
{
    in.x=x;in.y=y;in.pressed=in.held=true;assert(tick(16));
    in.pressed=in.held=false;in.released=true;assert(tick(16));in.released=false;
}
int main(void)
{
    assert(audio_vertical_keys(false,false)==AUDIO_KEYS_VOLUME);
    assert(audio_vertical_keys(false,true)==AUDIO_KEYS_BRIGHTNESS);
    assert(audio_vertical_keys(true,false)==AUDIO_KEYS_NONE && audio_vertical_keys(true,true)==AUDIO_KEYS_NONE);
    audio_controls_reset();assert(!tick(1) && !audio_controls_open() && !controls.drawn);
    in.up_pressed=in.up_held=true;tick(1);assert(movie.level.volume==25);in.up_pressed=false;
    tick(349);assert(movie.level.volume==25);tick(1);assert(movie.level.volume==30);
    in.up_held=false;tick(100);assert(movie.level.volume==30);
    in.x=0;in.y=200;tick(2000);tick(200);assert(!audio_controls_open());
    in.x=160;in.y=18;in.pressed=in.held=true;assert(!tick(16));
    in.pressed=in.held=false;in.released=true;in.x=300;in.y=200;assert(!tick(16));
    in.released=false;tick(200);assert(!audio_controls_open());
    in.up_pressed=in.up_held=true;tick(1);in.up_pressed=in.up_held=false;
    tick(200);assert(audio_controls_open());
    click(controls.track.x+2,controls.track.y);assert(movie.level.volume==100);
    in.x=controls.track.x+2;in.y=controls.track.y+controls.track.h;in.pressed=in.held=true;assert(tick(16));assert(movie.level.volume==0);
    assert(audio_controls_touchpad_captured());
    /* The physical switch remains down when the pad changes to an arrow zone. */
    PointerState pointer={.down=true};g_input_touchpad_report.pressed=true;
    for(int arrow=1;arrow<=4;++arrow) {
        g_input_touchpad_report.arrow=arrow;pointer_update_captured(&pointer);
        assert(pointer.down && !pointer.release_edge && !pointer.press_edge);
        in.pressed=false;in.held=pointer.down;assert(tick(16));assert(audio_controls_touchpad_captured());
    }
    in.pressed=false;in.x=-50;in.y=200;assert(tick(16) && movie.level.volume==0);
    in.x=400;in.y=-50;assert(tick(16) && movie.level.volume==100);
    g_input_touchpad_report.pressed=false;pointer_update_captured(&pointer);
    assert(!pointer.down && pointer.release_edge);
    in.held=pointer.down;in.released=pointer.release_edge;assert(tick(16));
    assert(!audio_controls_touchpad_captured());in.released=false;assert(!tick(16));
    tick(200);click(controls.speaker.x+10,controls.speaker.y+10);assert(movie.level.muted);
    tick(100);assert(controls.value_q8==0);
    click(controls.speaker.x+10,controls.speaker.y+10);assert(!movie.level.muted && movie.level.volume==100);
    tick(200);in.x=300;in.y=200;tick(2000);unsigned mix=controls.slide.current_mix;tick(50);
    assert(controls.slide.current_mix<mix);in.down_pressed=in.down_held=true;tick(1);in.down_pressed=false;
    unsigned closing=controls.slide.current_mix;tick(32);assert(controls.slide.current_mix>=closing && movie.level.volume==95);
    in.down_held=false;tick(200);
    /* Losing eligibility cancels an active drag while consuming its last input. */
    in.x=controls.track.x+2;in.y=controls.track.y+controls.track.h/2;in.pressed=in.held=true;assert(tick(16));
    in.pressed=false;in.enabled=false;assert(tick(16));assert(!audio_controls_open());
    in.held=false;in.enabled=true;tick(1);
    movie.available=false;assert(!tick(1) && !controls.drawn);
    movie.available=true;in.now_ms=UINT32_MAX-80;in.up_pressed=in.up_held=true;tick(1);
    in.up_pressed=in.up_held=false;tick(200);assert(audio_controls_open());
    audio_controls_reset();in=(AudioControlInput){.enabled=true,.pointer_visible=true,.now_ms=1000,.x=319,.y=45};
    tick(1);assert(!controls.slide.target_active);
    in.y=194;tick(1);assert(!controls.slide.target_active);
    in.x=318;in.y=100;tick(1);assert(!controls.slide.target_active);
    in.x=319;in.y=46;tick(1);assert(controls.slide.target_active);
    tick(200);assert(audio_controls_open());
    in.x=317;in.y=120;in.pressed=in.held=true;assert(tick(16));
    in.pressed=in.held=false;in.released=true;assert(tick(16));in.released=false;
    tick(200);assert(controls.slide.target_active);
    in.x=controls.track.x+2;tick(1900);assert(controls.slide.target_active);
    in.x=200;tick(2000);assert(!controls.slide.target_active);tick(200);assert(!audio_controls_open());
    audio_controls_reset();in.now_ms=UINT32_MAX-AUDIO_HIDE_MS+1;in.up_pressed=in.up_held=true;
    audio_controls_update(&movie,&in);assert(controls.have_until && controls.until_ms==0);
    in.up_pressed=in.up_held=false;tick(200);assert(audio_controls_open());
    puts("PASS vertical volume, bounded edge reveal and hover bridge, captured drag/mute, modifiers, retargeting and wraparound");
    return 0;
}
