from pathlib import Path
import shutil,subprocess,tempfile
from check_audio_controls import function
ROOT=Path(__file__).resolve().parents[1]
def check(work):
    work.mkdir(parents=True,exist_ok=True)
    source=work/'font.c'
    source.write_text((ROOT/'tools/tests/media/space_font.c').read_text().replace(
        '/* PRODUCTION_SPACE_FONT */',function((ROOT/'src/player/subtitles.c').read_text(),'repair_space_font')))
    exe=work/'font.exe'
    subprocess.run([shutil.which('gcc') or 'clang','-O2','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
if __name__=='__main__':
    with tempfile.TemporaryDirectory(prefix='ndvideo-space-font-') as d:check(Path(d))
