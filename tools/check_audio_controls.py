"""Exercise the actual audio HUD controller with the production UI easing."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile
import re

ROOT=Path(__file__).resolve().parents[1]
def function(source,name):
    match=re.search(r'^\w[^;{}]*?\b'+name+r'\s*\([^;]*?\)\s*\{',source,re.M)
    if not match:raise ValueError(name)
    end=source.index('{',match.start())+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]
def check(out):
    out.mkdir(parents=True,exist_ok=True)
    ui=(ROOT/'src/player/playback_ui.c').read_text()
    transitions='\n'.join(function(ui,name) for name in (
        'ui_ease_out_cubic','ui_ease_smoothstep','ui_transition_init',
        'ui_transition_value_with_ease','ui_transition_value','ui_transition_update_ex',
        'ui_transition_update','ui_transition_update_press_ex'))
    tests=ROOT/'tools/tests/media'
    source=out/'controls.c'
    source.write_text((tests/'audio_controls.c').read_text().replace(
        '"../../../src/player/audio_controls.c"','"'+(ROOT/'src/player/audio_controls.c').as_posix()+'"').replace(
        '/* PRODUCTION_TRANSITIONS */',transitions).replace(
        '/* PRODUCTION_CAPTURED_POINTER */',function(
            (ROOT/'src/player/input_timing_memory.c').read_text(),'pointer_update_captured')))
    exe=out/'controls.exe'
    subprocess.run([shutil.which('gcc') or 'clang','-std=c99','-O2','-Wall','-Wextra','-Werror',
        '-I',str(tests),'-I',str(ROOT/'src'),str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if args.output:check(args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='ndvideo-audio-controls-') as directory:check(Path(directory))
