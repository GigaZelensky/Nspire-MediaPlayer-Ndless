#!/usr/bin/env python3
"""Check the video info UI without opening files, decoding or changing clocks."""
from pathlib import Path
import os,shutil,subprocess,tempfile
from check_audio_controls import function
root=Path(__file__).resolve().parents[1]
source=(root/'src/player/video_info.c').read_text()
draw=function(source,'draw_video_info')
for forbidden in ('fopen(','nuc_','movie_file_read(','movie_file_seek(','performance_clock_start('):assert forbidden not in draw
loop=(root/'src/player/playback_loop.c').read_text()
assert loop.index('video_info_control(&info_open,false')<loop.index('if (!g_display_power_state.off && !g_display_power_state.off_fade_active && !help_menu_open && esc_down')
functions='\n'.join(function(source,n) for n in ('video_info_control','info_line','draw_video_info'))
fixture=(root/'tools/tests/media/video_info.c').read_text().replace('/* PRODUCTION_FUNCTIONS */',functions)
cc=os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
with tempfile.TemporaryDirectory(prefix='nmedia-video-info-') as tmp:
    p=Path(tmp)/'test.c';p.write_text(fixture);exe=Path(tmp)/'test.exe'
    subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'src'),str(p),str(root/'src/player/display_text.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=15)
