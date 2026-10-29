"""Run the production music loop and screenshot UI with simulated input and IO."""
from pathlib import Path
import argparse, shutil, subprocess
from check_audio_controls import function

ROOT = Path(__file__).resolve().parents[1]

def check(out):
    out.mkdir(parents=True, exist_ok=True)
    music = (ROOT / 'src/player/music_player.c').read_text()
    ui = (ROOT / 'src/player/music_ui.c').read_text()
    screenshot = (ROOT / 'src/player/screenshot_ui.c').read_text()
    internal = (ROOT / 'src/player/player_internal.h').read_text()
    start = internal.rfind('typedef struct {', 0, internal.index('} ScreenshotPreviewState;'))
    state = internal[start:internal.index('} ScreenshotPreviewState;') + len('} ScreenshotPreviewState;')]
    header = (ROOT / 'src/player/music_ui.h').read_text()
    state += '\n' + header[header.index('enum'):header.index('void music_ui_open')]
    fixture = (ROOT / 'tools/tests/media/music_screenshot.c').read_text()
    fixture = fixture.replace('/* PRODUCTION_STATE */', state)
    fixture = fixture.replace('/* PRODUCTION_SCREENSHOTS */', '\n'.join(
        function(screenshot, name) for name in (
            'screenshot_notice', 'screenshot_preview_animating', 'screenshot_preview_tick',
            'request_screenshot_in_directory', 'request_screenshot')))
    fixture = fixture.replace('/* PRODUCTION_MUSIC */', '\n'.join((
        function(ui, 'music_ui_close'), function(music, 'music_screen_power'),
        function(music, 'play_audio_file'))))
    # Ensure the real renderer shows feedback above cover/Info/visualizations,
    # independently of fading chrome, and before presenting the frame.
    render = function(ui, 'music_ui_render')
    assert render.index('expanded_art(') < render.index('draw_screenshot_preview_osd(') < render.index('present_screen(')
    cc = shutil.which('gcc') or shutil.which('clang')
    assert cc, 'Host C compiler required'
    source = out / 'music-screenshot.c'; source.write_text(fixture)
    exe = out / 'music-screenshot.exe'
    subprocess.run([cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(ROOT / 'src'), str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/music-screenshot-check')
    check(parser.parse_args().output)
