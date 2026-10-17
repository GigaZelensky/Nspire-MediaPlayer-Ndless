/* Exercise the production music UI update and button rasterizer. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define NDMEDIA_VISUALIZATIONS 0
#include "audio/audio_playback.h"
#include "audio/audio_metadata.h"
#include "player/music_controls.h"
#include "player/music_visual.h"

#define MAX_PATH_LEN 512
#define UI_HOVER_ANIM_MS 105U
#define UI_PRESS_ANIM_MS 65U
#define UI_PRESS_RELEASE_ANIM_MS 84U
#define SEEK_BADGE_EXIT_ANIM_MS 105U
#define STATUS_BADGE_EXIT_ANIM_MS 95U
typedef enum {PLAYBACK_MODE_ONCE, PLAYBACK_MODE_REPEAT, PLAYBACK_MODE_AUTO_NEXT} PlaybackMode;
typedef struct {
    bool initialized, target_active;
    uint8_t start_mix, current_mix;
    uint32_t started_ms;
} UiTransition;
typedef struct {
    int x, y;
    bool visible, moved, press_edge, release_edge, down;
} PointerState;
/* PRODUCTION_MUSIC_STATE */
/* PRODUCTION_TRANSITIONS */
static uint32_t monotonic_clock_now_ms(void) {return 0;}
/* PRODUCTION_MUSIC_UPDATE */

typedef uint16_t Uint16;
typedef struct {int x, y, w, h;} SDL_Rect;
typedef struct {void *white;} Fonts;
typedef struct {void *pixels; int pitch;} SDL_Surface;
typedef struct {Uint16 row_selected;} TestTheme;
static const TestTheme *ui_theme(void) {static TestTheme t; return &t;}
#define UI_COLOR_GUNMETAL 0
#define UI_COLOR_WHITE 65535
#define UI_COLOR_BLACK 0
/* Isolate the border's coverage from the body's glass gradient. */
static Uint16 rgb565_lerp(Uint16 a, Uint16 b, int t, int d) {
    (void)a; (void)b; (void)t; (void)d; return 0;
}
static Uint16 blend_rgb565(Uint16 a, Uint16 b, int t) {(void)a; (void)b; (void)t; return 0;}
static Uint16 pressed_control_base(Uint16 c, uint8_t p) {(void)p; return c;}
static Uint16 control_outline_color(Uint16 c, uint8_t m) {(void)c; (void)m; return 65535;}
static uint8_t max_u8(uint8_t a, uint8_t b) {return a > b ? a : b;}
static int pressed_control_offset_x(uint8_t p) {(void)p; return 0;}
static int pressed_control_offset_y(uint8_t p) {(void)p; return 0;}
#define NSP_FONT_HEIGHT 8
static int nSDL_GetStringWidth(void *f, const char *t) {(void)f; (void)t; return 0;}
static void draw_ui_label(SDL_Surface *s, const Fonts *f, int x, int y, const char *t) {
    (void)s; (void)f; (void)x; (void)y; (void)t;
}
static void fill_rect_rgb565(SDL_Surface *s, const SDL_Rect *r, Uint16 c) {
    for (int y = r->y; y < r->y + r->h; ++y)
        for (int x = r->x; x < r->x + r->w; ++x)
            ((Uint16 *)((uint8_t *)s->pixels + y * s->pitch))[x] = c;
}
/* PRODUCTION_MUSIC_BUTTON */

static void tick(MusicUi *ui, MusicControls *controls, PointerState *pointer, uint32_t now) {
    AudioPlaybackInfo info = {.rate = 44100, .duration_known = true};
    music_ui_update(ui, NULL, &info, controls, pointer, now);
}
static void check_hover(void) {
    MusicUi ui = {0}; MusicControls controls = {0};
    PointerState pointer = {.visible = true, .x = 30, .y = 165};
    assert(music_control_at(pointer.x, pointer.y, true) == MUSIC_MODE);
    tick(&ui, &controls, &pointer, 1000);
    tick(&ui, &controls, &pointer, 1105);
    assert(ui.hover[MUSIC_MODE].current_mix == 255 && !ui.tooltip.current_mix);
    tick(&ui, &controls, &pointer, 1399); assert(!ui.tooltip.target_active);
    tick(&ui, &controls, &pointer, 1400); assert(ui.tooltip.target_active && ui.animating);
    tick(&ui, &controls, &pointer, 1450); assert(ui.tooltip.current_mix > 0 && ui.tooltip.current_mix < 255);
    tick(&ui, &controls, &pointer, 1505); assert(ui.tooltip.current_mix == 255);
    /* A different edge button gets a fresh dwell; retain the old label only while fading out. */
    pointer.x = 280; assert(music_control_at(pointer.x, pointer.y, true) == MUSIC_VISUAL);
    tick(&ui, &controls, &pointer, 1600); assert(!ui.tooltip.target_active && ui.tooltip_target == MUSIC_MODE);
    tick(&ui, &controls, &pointer, 1705); assert(!ui.tooltip.current_mix);
    tick(&ui, &controls, &pointer, 1999); assert(!ui.tooltip.target_active);
    tick(&ui, &controls, &pointer, 2000); assert(ui.tooltip.target_active && ui.tooltip_target == MUSIC_VISUAL);
    /* Holding a button suppresses its label and restarts the dwell on release. */
    controls.capture = MUSIC_VISUAL; tick(&ui, &controls, &pointer, 2100);
    tick(&ui, &controls, &pointer, 4000); assert(!ui.tooltip.current_mix);
    controls.capture = MUSIC_NONE; tick(&ui, &controls, &pointer, 4399); assert(!ui.tooltip.target_active);
    tick(&ui, &controls, &pointer, 4400); assert(ui.tooltip.target_active);
    pointer.visible = false; tick(&ui, &controls, &pointer, 4500);
    tick(&ui, &controls, &pointer, 4605); assert(!ui.tooltip.current_mix);
    /* A quick pass never shows a label, including across the clock wrap. */
    memset(&ui, 0, sizeof(ui)); pointer.visible = true;
    tick(&ui, &controls, &pointer, UINT32_MAX - 200);
    tick(&ui, &controls, &pointer, 198); assert(!ui.tooltip.target_active);
    tick(&ui, &controls, &pointer, 199); assert(ui.tooltip.target_active);
}
static void check_corners(void) {
    Uint16 pixels[40 * 32] = {0};
    SDL_Surface screen = {pixels, 40 * 2}; Fonts fonts = {0};
    SDL_Rect button = {3, 3, 34, 26};
    music_button(&screen, &fonts, &button, "", 255, 0);
    #define PIXEL(x,y) pixels[(3 + (y)) * 40 + 3 + (x)]
    for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x) {
        assert(PIXEL(x,y) == PIXEL(y,x));
        assert(PIXEL(x,y) == PIXEL(33-x,y));
        assert(PIXEL(x,y) == PIXEL(x,25-y));
    }
    assert(PIXEL(0,0) == 0 && PIXEL(5,0) == 65535 && PIXEL(0,5) == 65535);
    assert(PIXEL(2,1) == 65535 && PIXEL(1,2) == 65535);
    assert((PIXEL(2,2) >> 11) >= 11 && PIXEL(4,4) == 0);
    for (int i = 0; i < 40; ++i) assert(!pixels[i] && !pixels[31*40+i]);
    puts("PASS delayed music labels, independent highlight/fade, retarget/hold/wrap and connected symmetric corner strokes");
}
int main(void) {check_hover(); check_corners(); return 0;}
