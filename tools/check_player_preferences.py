"""Exercise the real history format and global audio setters on the host."""
from pathlib import Path
import re,shutil,subprocess,tempfile
from check_usb_lifecycle import function
ROOT=Path(__file__).resolve().parents[1]

def check(out):
    out.mkdir(parents=True,exist_ok=True)
    header=(ROOT/'src/player/player_internal.h').read_text()
    types=[]
    for name,kind in [('ScaleMode','enum'),('VideoAlign','enum'),('PlaybackMode','enum'),
                      ('SubtitlePlacement','enum'),('UiThemeId','enum'),('HistoryEntry','struct'),('HistoryStore','struct')]:
        types.append(re.search(r'typedef '+kind+r'\s*\{[^{}]*\}\s*'+name+r';',header,re.S)[0])
    constants='\n'.join(line for line in header.splitlines() if line.startswith((
        '#define HISTORY_', '#define PLAYBACK_RATE_', '#define SUBTITLE_FONT_DEFAULT_INDEX ',
        '#define SUBTITLE_FONT_CHOICE_COUNT ')))
    history=(ROOT/'src/player/history_screenshots.c').read_text()
    source='\n'.join(function(history,n) for n in (
        'history_entry_init_defaults','history_store_init_defaults','history_parse_settings_line',
        'history_parse_clock_line','history_parse_audio_line','load_history_store_from_path',
        'save_history_store_to_path','history_save_clock_preference','load_app_preferences',
        'history_set_audio_preference','write_app_preferences','ui_queue_theme_save','flush_queued_preferences_save'))
    source+='\n'+function((ROOT/'src/audio/audio_playback.c').read_text(),'audio_playback_set_volume')
    fixture=(ROOT/'tools/tests/media/preferences.c').read_text()
    fixture=fixture.replace('/* PRODUCTION_TYPES */',constants+'\n'+'\n'.join(types))
    fixture=fixture.replace('/* PRODUCTION_PREFERENCES */',source)
    compiler=shutil.which('gcc') or shutil.which('clang')
    if not compiler:raise RuntimeError('A host C compiler is required')
    c=out/'preferences.c';c.write_text(fixture)
    for audio in (0,1):
        exe=out/('preferences'+str(audio)+'.exe')
        subprocess.run([compiler,'-std=c99','-O2','-Wall','-Wextra','-Werror',
            '-DNDMEDIA_AUDIO='+str(audio),str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(out/'history.tns')],check=True,timeout=10)

if __name__=='__main__':
    with tempfile.TemporaryDirectory(prefix='nmedia-preferences-') as folder:check(Path(folder))
