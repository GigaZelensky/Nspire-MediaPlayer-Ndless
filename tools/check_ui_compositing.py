#!/usr/bin/env python3
"""Regression coverage for colored-wallpaper overlays and loading frames."""
from pathlib import Path
import os,shutil,subprocess,tempfile
from check_audio_controls import function
root=Path(__file__).resolve().parents[1]
source=(root/'src/player/render_primitives.c').read_text()
functions='\n'.join(function(source,n) for n in ('rgb565_lerp','blend_rgb565','fill_rect_rgb565',
    'fill_rect_rgb565_mix','blit_surface_rgb565_mix','dim_rect_rgb565','draw_vertical_gradient','fill_picker_selection_line'))
loading=function((root/'src/player/picker_ui.c').read_text(),'draw_loading_transition_frame')
reference=loading.replace('draw_loading_transition_frame(','reference_loading(',1)
for line in ('    SDL_Rect old_clip;\n','    SDL_GetClipRect(screen,&old_clip);\n','    SDL_SetClipRect(screen,NULL);\n','    SDL_SetClipRect(screen,&old_clip);\n'):
    reference=reference.replace(line,'')
text=(root/'tools/tests/media/compositing.c').read_text().replace('/* PRODUCTION_FUNCTIONS */',functions+'\n'+loading+'\n'+reference)
cc=os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
with tempfile.TemporaryDirectory(prefix='nmedia-compositing-') as tmp:
    p=Path(tmp)/'test.c';p.write_text(text);exe=Path(tmp)/'test.exe'
    subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=15)
