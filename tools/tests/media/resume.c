#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef struct { bool alive; } SDL_Surface;
typedef struct { int unused; } Fonts;
typedef struct { int unused; } Movie;
typedef struct {
    SDL_Surface *screen, *snapshot;
    const Fonts *fonts;
    const char *label;
    uint32_t started_ms, last_draw_ms;
    bool dim_background;
    void (*render)(void *,uint32_t);
    bool (*cancelled)(void *);
    void *context;
} LoadingProgress;
enum { UI_LOADING_PROGRESS_FRAME_MS=16 };
static unsigned now_ms=1000, audio_opens, redraws;
static unsigned custom_draws;
static void custom_draw(void *context,uint32_t now){assert(context==&custom_draws && now==now_ms);++custom_draws;}
static uint32_t monotonic_clock_now_ms(void) { return now_ms; }
static void clock_menu_queue_shortcut(void) {}
static void draw_loading_transition_frame(SDL_Surface *screen, SDL_Surface *snapshot,
    const Fonts *fonts, const char *label, unsigned mix, int phase, bool opening, bool dim)
{
    (void)screen; (void)fonts; (void)label; (void)mix; (void)phase; (void)opening; (void)dim;
    assert(!snapshot || snapshot->alive);
    ++redraws;
}
/* PRODUCTION_LOADING */
static void draw_movie_frame_background(SDL_Surface *s, Movie *m, int scale, int x, int y,
                                         void *a, void *b)
{ (void)m; (void)scale; (void)x; (void)y; (void)a; (void)b; assert(s->alive); }
static void movie_audio_open(Movie *m, const char *path, LoadingProgress *progress)
{
    (void)m; (void)path;
    /* Audio decoding invokes progress after its input reads. */
    now_ms+=100;
    loading_progress_tick(progress, true);
    ++audio_opens;
}
static void run(bool prompt_consumes_snapshot)
{
    SDL_Surface screen_value={true}, backdrop={true};
    SDL_Surface *screen=&screen_value, *loading_snapshot=&backdrop;
    Fonts font_value={0}; const Fonts *fonts=&font_value;
    Movie movie={0}; const char *path="usb:/ndvideo/resume.nvp.tns";
    int scale_mode=0, video_align_x=0, video_align_y=0;
    LoadingProgress loading_progress;
    loading_progress_init(&loading_progress,screen,loading_snapshot,fonts,"Loading",true);
    assert(!loading_progress.render && !loading_progress.cancelled && !loading_progress.context);
    if(prompt_consumes_snapshot) {
        /* Tombstone the surface consumed by the real prompt. Reusing it in
         * the renderer fails deterministically without dereferencing freed RAM. */
        backdrop.alive=false;
        loading_snapshot=NULL;
    }
    /* PRODUCTION_AUDIO_START */
}
int main(void)
{
    run(true);
    run(false);
    assert(audio_opens==2 && redraws==2);
    SDL_Surface s={true};Fonts fonts={0};LoadingProgress l;
    loading_progress_init(&l,&s,NULL,&fonts,"unused",true);
    l.render=custom_draw;l.context=&custom_draws;
    loading_progress_tick(&l,true);
    assert(custom_draws==1 && redraws==2);
    puts("PASS resume-to-audio handoff redraws only the current live backdrop after the modal consumes its snapshot");
    return 0;
}
