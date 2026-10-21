"""Regression checks for delayed theme preparation, fade endpoints and USB repaint cost."""
from pathlib import Path
import os,shutil,subprocess,tempfile
from check_audio_controls import function
root=Path(__file__).resolve().parents[1]
header=(root/'src/player/player_internal.h').read_text()
render=(root/'src/player/render_primitives.c').read_text()
ui=(root/'src/player/playback_ui.c').read_text()
browser=(root/'src/player/usb_browser.c').read_text()
def structure(start,end):
    at=header.index(start);return header[at:header.index(end,at)+len(end)]
source='''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint16_t Uint16;
#define UI_RGB565(r,g,b) ((Uint16)(((r&248)<<8)|((g&252)<<3)|(b>>3)))
#define UI_CHROME_ANIM_MS 115
#define UI_CHROME_EXIT_ANIM_MS 200
#define UI_WAKE_MIN_MIX 28
static uint32_t clock_ms=1000;static unsigned prepares;static bool cached=true;
static uint32_t monotonic_clock_now_ms(void){return clock_ms;}
static int wallpaper_cached(void){return cached;}
static const uint16_t *wallpaper_image(uint16_t accent){(void)accent;++prepares;clock_ms+=350;return NULL;}
static int clamp_int(int n,int a,int b){return n<a?a:n>b?b:n;}
'''
source+=structure('typedef enum {\n    UI_THEME_AMBER','} UiThemeId;')
source+=structure('typedef struct {\n    const char *name;\n    Uint16 accent_hot;','} UiThemePalette;')
source+=structure('typedef struct {\n    bool initialized;','} UiTransition;')
source+=structure('typedef struct {\n    uint8_t chrome;','} PlaybackUiMixes;')
source+=render[render.index('static const UiThemePalette g_ui_themes'):render.index('UiThemeId ui_theme_clamp')]
source+='\n'.join(function(render,n) for n in ('rgb565_lerp','ui_theme_clamp','ui_theme_transition_ease','ui_theme_blend_palette','ui_theme_transition_tick','ui_theme','ui_theme_target','ui_cycle_theme','ui_theme_transition_active'))
source+='\n'.join(function(ui,n) for n in ('ui_ease_out_cubic','ui_ease_smoothstep','ui_transition_init','ui_transition_value_with_ease','ui_transition_chrome','ui_mix_animating','playback_ui_mixes_animating'))
source+='''
typedef struct {uint32_t last_draw;bool painted,draw_dirty;uint8_t last_mix,painted_hover,painted_press;} UsbConnectWindow;
'''+function(browser,'connection_repaint_due')
source+='''
int main(void)
{
    uint16_t old=ui_theme()->accent_mid;
    ui_cycle_theme();assert(prepares==1 && clock_ms==1350);
    assert(ui_theme_transition_active() && ui_theme()->accent_mid==old);
    clock_ms+=90;assert(ui_theme()->accent_mid!=old && ui_theme()->accent_mid!=ui_theme_target()->accent_mid);
    clock_ms+=90;assert(!ui_theme_transition_active() && ui_theme()->accent_mid==ui_theme_target()->accent_mid);
    bool saw_purple=false;
    for(unsigned i=0;i<UI_THEME_COUNT;++i) {
        uint16_t outgoing=ui_theme()->accent_mid;
        ui_cycle_theme();assert(ui_theme_transition_active() && ui_theme()->accent_mid==outgoing);
        clock_ms+=180;assert(!ui_theme_transition_active());
        if(g_ui_theme_id==UI_THEME_PURPLE) {
            saw_purple=true;
            uint16_t c=ui_theme()->accent_mid;
            assert((c&31U)>((c>>11)&31U) && ((c>>11)&31U)>((c>>6)&31U));
        }
    }
    assert(saw_purple && g_ui_theme_id==UI_THEME_BLUE);
    cached=false;unsigned before=prepares;ui_cycle_theme();assert(prepares==before); /* Video keeps no wallpaper. */
    UiTransition chrome;ui_transition_init(&chrome,true);
    assert(ui_transition_chrome(&chrome,false,2000)==255 && !chrome.target_active);
    PlaybackUiMixes mixes={.chrome=255,.transitioning=true};assert(playback_ui_mixes_animating(&mixes));
    unsigned previous=255,intermediate=0;
    for(unsigned at=0;at<=208;at+=16){unsigned m=ui_transition_chrome(&chrome,false,2000+at);assert(m<=previous);previous=m;if(m && m<255)++intermediate;}
    assert(previous==0 && intermediate>=10);
    assert(ui_transition_chrome(&chrome,true,2300)==28);
    assert(ui_transition_chrome(&chrome,true,2415)==255);
    mixes=(PlaybackUiMixes){0};assert(!playback_ui_mixes_animating(&mixes));
    UsbConnectWindow window={0};unsigned draws=0,polls=0;
    for(unsigned at=0;at<1000;at+=10){++polls;if(connection_repaint_due(&window,false,at,255,0,0)){++draws;window.painted=true;window.last_draw=at;window.last_mix=255;}}
    assert(polls==100 && draws<=6); /* Every service turn polls input; stable UI is 5.6Hz. */
    window.draw_dirty=true;assert(connection_repaint_due(&window,false,1000,255,0,0));
    window.last_draw=1000;assert(!connection_repaint_due(&window,false,1001,255,0,0));
    assert(connection_repaint_due(&window,true,1001,255,0,0));
    window.draw_dirty=false;assert(connection_repaint_due(&window,false,1016,255,1,0));
    puts("PASS theme fade starts after serviced texture work; video allocates no wallpaper; visible exit endpoints and unchanged entrance; USB polling independent of bounded repaint cadence");
}
'''
loop=(root/'src/player/playback_loop.c').read_text()
assert 'ui_transition_init(&ui_transitions.chrome, false)' not in loop
window=function(browser,'render_window')
assert window.index('pointer_update(pointer)')<window.index('connection_repaint_due(')
assert window.index('window->cancelled = true')<window.index('connection_repaint_due(')
assert 'render_window(context, false)' in function(browser,'progress')
cc=os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
with tempfile.TemporaryDirectory(prefix='nmedia-ui-transition-') as tmp:
    c=Path(tmp)/'test.c';c.write_text(source);exe=Path(tmp)/'test.exe'
    subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=15)
