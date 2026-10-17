#!/usr/bin/env python3
"""Build the portable Windows GUI with its native FFmpeg runtime."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys,zipfile,io
from importlib.metadata import distribution

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'dist/encoder')
    args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    work=ROOT/'build/encoder-desktop';work.mkdir(parents=True,exist_ok=True)
    # All generated build files stay in this explicit workspace build folder.
    assert work.resolve().is_relative_to(ROOT.resolve())
    from PySide6.QtCore import QByteArray,QBuffer,QIODevice
    from PySide6.QtGui import QImage,QPainter
    from PySide6.QtSvg import QSvgRenderer
    from PIL import Image
    image=QImage(256,256,QImage.Format.Format_ARGB32);image.fill(0)
    painter=QPainter(image);QSvgRenderer(str(ROOT/'tools/encoder_gui/assets/app.svg')).render(painter);painter.end()
    data=QByteArray();buffer=QBuffer(data);buffer.open(QIODevice.OpenModeFlag.WriteOnly);image.save(buffer,'PNG')
    app_icon=work/'app.ico';Image.open(io.BytesIO(bytes(data))).save(app_icon,format='ICO',sizes=[(n,n) for n in (16,24,32,48,64,128,256)])
    licenses=work/'licenses';licenses.mkdir(exist_ok=True)
    for name in ('PySide6-Essentials','shiboken6','PyInstaller','Pillow','cryptography','psutil','imageio-ffmpeg'):
        dist=distribution(name)
        for file in dist.files or []:
            if any(part.lower() in ('licenses','license','copying','copyright') or part.lower().startswith(('license.','copying.','copyright.')) for part in file.parts):
                source=Path(dist.locate_file(file))
                if source.is_file():
                    target=licenses/name/Path(*file.parts)
                    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    shutil.copy2(ROOT/'LICENSE',licenses/'nMedia-GPL-3.0.txt')
    shutil.copy2(ROOT/'tools/encoder_gui/assets/PublicSans-OFL.txt',licenses/'PublicSans-OFL.txt')
    command=[sys.executable,'-m','PyInstaller','--noconfirm','--clean','--onefile','--windowed',
        '--name','nMedia-Encoder-Windows-x64','--distpath',str(output),'--workpath',str(work/'objects'),
        '--icon',str(app_icon),
        '--specpath',str(work),'--paths',str(ROOT/'tools'),'--collect-all','imageio_ffmpeg',
        '--add-data',str(ROOT/'tools/encoder_gui/assets')+os.pathsep+'encoder_gui/assets',
        '--add-data',str(licenses)+os.pathsep+'encoder_gui/licenses',
        '--hidden-import','encode_ndless_video','--hidden-import','av1_stream','--hidden-import','media_tracks',
        '--hidden-import','audio_track','--hidden-import','check_encoder_gui',
        '--hidden-import','nvp_tracks','--hidden-import','edit_ndless_video','--hidden-import','check_nvp_tracks',
        '--exclude-module','numpy','--exclude-module','PySide6.QtNetwork','--exclude-module','PySide6.QtQml',
        '--exclude-module','PySide6.QtQuick','--exclude-module','PySide6.QtWebEngineCore',str(ROOT/'tools/encode_gui.py')]
    environment=os.environ.copy()
    if os.name=='nt':
        # Resolve platform runtimes from Windows/Python, never from a JDK,
        # unrelated Qt installation or compiler which happens to be on PATH.
        windows=Path(os.environ.get('SystemRoot','C:/Windows'))
        environment['PATH']=os.pathsep.join(str(p) for p in (
            Path(sys.executable).parent,Path(sys.base_prefix),windows/'System32',windows))
    subprocess.run(command,cwd=ROOT,env=environment,check=True)
    exe=output/'nMedia-Encoder-Windows-x64.exe'
    if os.name!='nt':exe=output/'nMedia-Encoder-Windows-x64'
    digest=hashlib.sha256(exe.read_bytes()).hexdigest()
    shutil.copy2(ROOT/'LICENSE',output/'nMedia-Encoder-LICENSE.txt')
    with zipfile.ZipFile(output/'nMedia-Encoder-Licenses.zip','w',zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(licenses.rglob('*')):
            if path.is_file():archive.write(path,path.relative_to(licenses).as_posix())
    notes='''nMedia Encoder

Open nMedia-Encoder-Windows-x64.exe. No Python installation is required.
Choose or drop a source video, choose audio/subtitles, then Encode video.
The encoder runs on your computer and does not upload your media.
Use the existing tools/encode_ndless_video.py for command-line encoding.

Source and build instructions:
https://github.com/GigaZelensky/Nspire-MediaPlayer-Ndless

Dependencies (included in the executable):
PySide6 / Qt 6.12.0, GPLv3 or LGPLv3: https://code.qt.io/cgit/pyside/pyside-setup.git/
Qt sources and licenses: https://code.qt.io/cgit/qt/qtbase.git/
PyInstaller bootloader exception: https://pyinstaller.org/en/stable/license.html
imageio-ffmpeg 0.6.0 (BSD-2-Clause wrapper): https://github.com/imageio/imageio-ffmpeg
Its native FFmpeg build is GPL-licensed: https://github.com/imageio/imageio-binaries
FFmpeg source: https://ffmpeg.org/download.html
Pillow (HPND): https://github.com/python-pillow/Pillow
cryptography (Apache-2.0 or BSD): https://github.com/pyca/cryptography
psutil (BSD-3-Clause): https://github.com/giampaolo/psutil

Public Sans (SIL Open Font License): https://github.com/uswds/public-sans
The nMedia application source is distributed under the included GPL-3.0 license.
Dependency license texts are included in nMedia-Encoder-Licenses.zip and the executable.
'''
    (output/'nMedia-Encoder-README.txt').write_text(notes,encoding='utf8')
    sums=''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in sorted(output.iterdir()) if p.is_file() and p.name!='nMedia-Encoder-SHA256SUMS.txt')
    (output/'nMedia-Encoder-SHA256SUMS.txt').write_text(sums,encoding='utf8')
    print(json.dumps({'executable':str(exe),'bytes':exe.stat().st_size,'sha256':digest},indent=2))

if __name__=='__main__':main()
