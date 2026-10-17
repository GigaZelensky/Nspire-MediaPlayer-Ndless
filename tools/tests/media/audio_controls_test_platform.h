#ifndef AUDIO_CONTROLS_TEST_PLATFORM_H
#define AUDIO_CONTROLS_TEST_PLATFORM_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio/movie_audio.h"
typedef uint16_t Uint16;
typedef struct {int16_t x,y;uint16_t w,h;} SDL_Rect;
typedef struct {uint32_t guard_before;uint16_t pixels[320*240];uint32_t guard_after;} SDL_Surface;
typedef struct {void *white;} Fonts;
struct AudioPlayback {bool available;AudioPlaybackLevel level;};
typedef AudioPlayback Movie;
typedef struct {bool initialized,target_active;uint8_t start_mix,current_mix;uint32_t started_ms;} UiTransition;
typedef struct {Uint16 progress_fill_top,progress_fill_bottom,progress_fill_glow;} Palette;
enum {UI_HOVER_ANIM_MS=105,UI_PRESS_ANIM_MS=120,PICKER_PRESS_RELEASE_ANIM_MS=94,NSP_FONT_HEIGHT=8};
#define UI_COLOR_GUNMETAL 0x2924
#define UI_COLOR_WARM_WHITE 0xff59
#define UI_COLOR_ACCENT 0xfa80
#define UI_COLOR_WHITE 0xffff
int clamp_int(int,int,int);
uint8_t ui_ease_out_cubic(uint32_t,uint32_t);
uint8_t ui_ease_smoothstep(uint32_t,uint32_t);
uint8_t ui_transition_update(UiTransition *,bool,uint32_t,uint32_t);
uint8_t ui_transition_update_press_ex(UiTransition *,bool,uint32_t,uint32_t,uint32_t);
int pressed_control_offset_x(uint8_t);
int pressed_control_offset_y(uint8_t);
void fill_rect_rgb565(SDL_Surface *,const SDL_Rect *,Uint16);
void draw_soft_glass_panel_body_from_y(SDL_Surface *,const SDL_Rect *,int,Uint16);
void draw_soft_glass_panel_rim(SDL_Surface *,const SDL_Rect *,Uint16,uint8_t);
void draw_prompt_button(SDL_Surface *,const Fonts *,const SDL_Rect *,const char *,uint8_t,uint8_t);
void draw_progress_track(SDL_Surface *,const SDL_Rect *,const SDL_Rect *);
void draw_vertical_gradient(SDL_Surface *,const SDL_Rect *,Uint16,Uint16);
void draw_ui_label(SDL_Surface *,const Fonts *,int,int,const char *);
int nSDL_GetStringWidth(void *,const char *);
const Palette *ui_theme(void);
#endif
