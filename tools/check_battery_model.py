#!/usr/bin/env python3
"""Check continuous default estimates and conservative raw-observation learning."""
from pathlib import Path
import os, shutil, subprocess, tempfile
from check_audio_controls import function
ROOT=Path(__file__).resolve().parents[1]
cc=os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
if not cc:raise SystemExit('A host C compiler is required')
with tempfile.TemporaryDirectory(prefix='nmedia-battery-model-') as tmp:
    exe=Path(tmp)/'model.exe'
    subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror',
        '-I'+str(ROOT/'src/platform'),str(ROOT/'tools/tests/media/battery_model.c'),
        str(ROOT/'src/platform/battery_model.c'),str(ROOT/'src/platform/battery_curve.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=15)
    exe=Path(tmp)/'meter.exe'
    subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror',
        '-I'+str(ROOT/'src/platform'),str(ROOT/'tools/tests/media/battery_meter.c'),
        str(ROOT/'src/platform/battery_model.c'),str(ROOT/'src/platform/battery_curve.c'),str(ROOT/'src/platform/battery_meter.c'),
        '-o',str(exe)],check=True)
    subprocess.run([str(exe)],cwd=tmp,check=True,timeout=15)
    assert {p.name for p in Path(tmp).glob('*.tns')}=={'nMedia-battery-a.dat.tns','nMedia-battery-b.dat.tns'}
    power=(ROOT/'src/platform/battery_power.c').read_text()
    fixture=(ROOT/'tools/tests/media/battery_power.c').read_text().replace('/* PRODUCTION_SAMPLE */',
        function(power,'battery_power_sample')+'\n'+function(power,'battery_usb_power'))
    source=Path(tmp)/'power.c';source.write_text(fixture);exe=Path(tmp)/'power.exe'
    subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src/platform'),
        str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=15)
