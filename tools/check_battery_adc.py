#!/usr/bin/env python3
"""Exercise bounded ADC acquisition and the explicitly feature-disabled path."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
if not compiler:
    raise SystemExit('A host C compiler is required.')
with tempfile.TemporaryDirectory(prefix='nmedia-battery-adc-') as temporary:
    for enabled in (0, 1):
        executable = Path(temporary) / f'adc-{enabled}.exe'
        subprocess.run([compiler, '-std=c99', '-DNDMEDIA_BATTERY_SERVICE=0', '-DNDMEDIA_BATTERY_METER=0', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-DNDVIDEO_BATTERY_ACQUISITION=' + str(enabled),
                        '-I' + str(ROOT / 'src/platform'),
                        str(ROOT / 'tools/tests/media/battery_adc_poll.c'),
                        str(ROOT / 'src/platform/battery_adc_poll.c'), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=10)
