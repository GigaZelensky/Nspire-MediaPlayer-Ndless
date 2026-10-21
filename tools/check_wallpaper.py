#!/usr/bin/env python3
"""Verify packed raster data and the production fixed-point theme transform."""
from pathlib import Path
import os,re,shutil,struct,subprocess,tempfile,zlib
from PIL import Image
root=Path(__file__).resolve().parents[1]
packed=bytes(int(v,16) for v in re.findall(r'0x([0-9a-f]{2})',(root/'src/player/wallpaper_data.inc').read_text()))
image=Image.open(root/'assets/player-wallpaper.png').convert('RGB')
assert image.size==(320,240)
raw=b''.join(struct.pack('<H',((r>>3)<<11)|((g>>2)<<5)|(b>>3)) for r,g,b in image.getdata())
assert len(raw)==153600 and zlib.decompress(packed)==raw
cc=os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
if not cc:raise SystemExit('Host C compiler required')
with tempfile.TemporaryDirectory(prefix='nmedia-wallpaper-') as tmp:
    exe=Path(tmp)/'wallpaper.exe'
    subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'src'),
        str(root/'tools/tests/media/wallpaper.c'),str(root/'src/player/wallpaper_color.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=15)
    if os.name!='nt':
        cache=Path(tmp)/'cache'
        subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'src'),
            str(root/'tools/tests/media/wallpaper_cache.c'),str(root/'src/player/wallpaper_color.c'),
            str(root/'src/player/wallpaper_image.c'),'-Wl,--wrap=uncompress','-lz','-o',str(cache)],check=True)
        subprocess.run([str(cache)],check=True,timeout=15)
print(f'PASS packed wallpaper exact RGB565 match: {len(packed)} bytes stored, 153600-byte steady cache, a second only during theme crossfade')
