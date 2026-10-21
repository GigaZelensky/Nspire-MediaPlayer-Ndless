#!/usr/bin/env python3
"""Pure estimator checks; no firmware access or built-in family-wide curve."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
cc=os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
if not cc:raise SystemExit('A host C compiler is required.')
with tempfile.TemporaryDirectory(prefix='nmedia-battery-gauge-') as temporary:
    for enabled in (0,1):
        out=Path(temporary)/f'gauge-{enabled}.exe'
        subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror',
            '-DNDMEDIA_BATTERY_GAUGE_TEST='+str(enabled),'-I'+str(ROOT/'src/platform'),
            str(ROOT/'tools/tests/media/battery_gauge.c'),str(ROOT/'src/platform/battery_gauge.c'),'-o',str(out)],check=True)
        subprocess.run([str(out)],check=True,timeout=10)
