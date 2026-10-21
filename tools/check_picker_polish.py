"""Exercise breadcrumb dwell/wrapping and exclusive picker hover ownership."""
from pathlib import Path
import os, shutil, subprocess, tempfile
from check_audio_controls import function

root=Path(__file__).resolve().parents[1]
nav=(root/'src/player/picker_navigation.c').read_text()
tip=(root/'src/player/picker_tooltip.c').read_text()
ui=(root/'src/player/playback_ui.c').read_text()
header=(root/'src/player/player_internal.h').read_text()
types=header[header.index('typedef struct {\n    bool initialized;'):]
types=types[:types.index('} UiTransition;')+len('} UiTransition;')]
source=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint16_t Uint16;
typedef int nSDL_Font;
typedef struct {int x,y,w,h;} SDL_Rect;
typedef struct {int unused;} SDL_Surface;
typedef struct {nSDL_Font *white;} Fonts;
typedef struct {int x,y;bool visible,press_edge,down;} PointerState;
typedef struct {int kind;char *name,*detail;bool resume_time_known;uint32_t resume_ms,duration_ms;uint64_t file_bytes;} MovieFile;
enum {PICKER_FOLDER,PICKER_SEPARATOR,PICKER_AUDIO,PICKER_MOVIE};
enum {PICKER_NAV_NONE,PICKER_NAV_BACK,PICKER_NAV_USB};
#define MAX_PATH_LEN 512
#define SCREEN_W 320
#define SCREEN_H 240
#define NSP_FONT_HEIGHT 7
#define PICKER_TOOLTIP_DWELL_MS 450
#define UI_TOOLTIP_ANIM_MS 115
#define UI_HOVER_ANIM_MS 105
#define UI_PRESS_ANIM_MS 120
#define PICKER_PRESS_RELEASE_ANIM_MS 94
#define UI_PRESS_PRIME_MIX 72
#define UI_PRESS_RELEASE_VISIBLE_MIX 255
#define UI_COLOR_UNSUPPORTED 1
static bool media_browser_at_root(void){return false;}
static bool rect_contains_point(const SDL_Rect *r,int x,int y){return x>=r->x && y>=r->y && x<r->x+r->w && y<r->y+r->h;}
static SDL_Rect button_rect(const Fonts *f,unsigned i,int offset){(void)f;return i?(SDL_Rect){130,15+offset,40,13}:(SDL_Rect){8,28+offset,31,14};}
static bool available(unsigned i){(void)i;return true;}
static unsigned clamp_int(int n,int a,int b){return n<a?a:n>b?b:n;}
static int font_char_advance_width(nSDL_Font *f,unsigned char c){(void)f;(void)c;return 6;}
static int nSDL_GetStringWidth(nSDL_Font *f,const char *s){(void)f;return (int)strlen(s)*6;}
static unsigned picker_text_color(bool secondary){return secondary?2:3;}
static bool picker_codec_unavailable(const MovieFile *f){(void)f;return false;}
static void format_clock(uint32_t t,char *s,size_t n){snprintf(s,n,"%lu",(unsigned long)t);}
static struct {unsigned tooltip_base;} theme;
#define ui_theme() (&theme)
static unsigned label_count;
static char labels[16][96];
static SDL_Rect last_panel;
static SDL_Surface *begin_faded_region_draw(SDL_Surface *s,const SDL_Rect *r,uint8_t mix,SDL_Surface **out,SDL_Rect *clip,int *dx,int *dy)
{(void)mix;*out=s;*clip=*r;last_panel=*r;*dx=*dy=0;return NULL;}
static void end_faded_region_draw(SDL_Surface *s,SDL_Surface *layer,const SDL_Rect *r,const SDL_Rect *clip,uint8_t mix)
{(void)s;(void)layer;(void)r;(void)clip;(void)mix;}
static SDL_Rect offset_sdl_rect(const SDL_Rect *r,int dx,int dy){SDL_Rect q=*r;q.x+=dx;q.y+=dy;return q;}
static void draw_soft_glass_panel_body_from_y(SDL_Surface *s,const SDL_Rect *r,int y,unsigned c){(void)s;(void)r;(void)y;(void)c;}
static void draw_soft_glass_panel_rim(SDL_Surface *s,const SDL_Rect *r,unsigned c,unsigned mix){(void)s;(void)r;(void)c;(void)mix;}
static void draw_ui_label_tint(SDL_Surface *s,const Fonts *f,int x,int y,const char *t,unsigned c,unsigned mix)
{(void)s;(void)f;(void)c;(void)mix;assert(label_count<16 && strlen(t)<96);assert(x>=4 && y>=4 && x+(int)strlen(t)*6<=316 && y+7<=236);memcpy(labels[label_count++],t,strlen(t)+1);}
'''+types+'\n'
source+='\n'.join(function(ui,n) for n in ('ui_ease_out_cubic','ui_ease_smoothstep','ui_transition_init','ui_transition_value_with_ease','ui_transition_value','ui_transition_update_ex','ui_transition_update','ui_transition_update_press_ex','ui_transition_begin_press_release'))
source+='\nstatic UiTransition hover[2],press[2],row_focus,path_tooltip;\nstatic int captured=-1;static bool keyboard,path_hot;static uint32_t path_since;\n'
source+='\n'.join(function(nav,n) for n in ('picker_navigation_reset','picker_navigation_poll','picker_navigation_focus_rows','picker_navigation_rows_mix'))
source+='\n'+tip[tip.index('enum {'):tip.index('static bool tooltip_space')]
source+='\n'.join(function(tip,n) for n in ('format_file_size','tooltip_space','tooltip_append','tooltip_truncate','draw_tooltip','draw_path_hover_tooltip','draw_movie_hover_tooltip'))
source+=r'''
int main(void)
{
    Fonts f={0};PointerState p={.visible=true,.x=80,.y=34};bool consumed;
    picker_navigation_reset();assert(picker_navigation_rows_mix(1000)==255);
    picker_navigation_poll(&f,&p,false,false,false,1000,&consumed);assert(consumed && path_hot && !path_tooltip.target_active);
    picker_navigation_poll(&f,&p,false,false,false,1449,&consumed);assert(!path_tooltip.target_active);
    picker_navigation_poll(&f,&p,false,false,false,1450,&consumed);assert(path_tooltip.target_active);
    picker_navigation_poll(&f,&p,false,false,false,1600,&consumed);assert(path_tooltip.current_mix==255);
    picker_navigation_focus_rows(false,1600);assert(picker_navigation_rows_mix(1710)==0);
    p.y=85;picker_navigation_poll(&f,&p,false,false,false,1720,&consumed);assert(!consumed && !path_hot);
    picker_navigation_focus_rows(true,1720);assert(picker_navigation_rows_mix(1830)==255);
    p.x=20;p.y=34;picker_navigation_poll(&f,&p,false,false,false,1900,&consumed);assert(consumed && !path_hot);
    p.x=140;p.y=20;picker_navigation_poll(&f,&p,false,false,false,2000,&consumed);assert(consumed && !path_hot);
    /* Resume is part of its row, so its owner stays active. */
    picker_navigation_focus_rows(true,2010);assert(picker_navigation_rows_mix(2120)==255);
    char path[512];snprintf(path,sizeof(path),"USB / Music / Joaquin Sabina / Fisica y Quimica / A long subfolder / ");
    while(strlen(path)<490)strcat(path,"folder/");
    strcat(path,"END");SDL_Surface screen={0};p.x=280;p.y=34;
    draw_path_hover_tooltip(&screen,&f,path,&p,255);
    assert(label_count>7 && last_panel.w<=244 && last_panel.h<=220);
    char reconstructed[1024]={0};for(unsigned i=0;i<label_count;++i)strcat(reconstructed,labels[i]);
    assert(strstr(reconstructed,"END") && !strstr(reconstructed,"..."));
    label_count=0;MovieFile song={.kind=PICKER_AUDIO,.name="Song",.detail="FLAC",.file_bytes=4*1024*1024};
    draw_movie_hover_tooltip(&screen,&f,&song,&p,255);assert(!strcmp(labels[label_count-1],"4.0 MiB"));
    puts("PASS exclusive row/header/control hover, breadcrumb dwell/leave, bounded full-path wrapping and IEC detail units");
}
'''
cc=os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
with tempfile.TemporaryDirectory(prefix='nmedia-picker-polish-') as tmp:
    c=Path(tmp)/'test.c';c.write_text(source);exe=Path(tmp)/'test.exe'
    subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=15)
