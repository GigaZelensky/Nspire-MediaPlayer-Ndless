#!/usr/bin/env python3
"""Pack the selected wallpaper once; calculator drawing never reads a PNG."""
from pathlib import Path
import struct,zlib
from PIL import Image
root=Path(__file__).resolve().parents[1]
image=Image.open(root/'assets/player-wallpaper.png').convert('RGB')
if image.size!=(320,240):raise SystemExit('Wallpaper must be 320x240')
raw=b''.join(struct.pack('<H',((r>>3)<<11)|((g>>2)<<5)|(b>>3)) for r,g,b in image.getdata())
packed=zlib.compress(raw,9)
text='/* Generated from assets/player-wallpaper.png; RGB565 LE, zlib. */\nstatic const unsigned char wallpaper_data[]={\n'
text+=''.join('    '+','.join('0x%02x'%v for v in packed[i:i+20])+',\n' for i in range(0,len(packed),20))+'};\n'
(root/'src/player/wallpaper_data.inc').write_text(text)
print(f'Packed {len(raw)} wallpaper bytes into {len(packed)} bytes')
