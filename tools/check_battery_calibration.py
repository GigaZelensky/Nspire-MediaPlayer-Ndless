#!/usr/bin/env python3
"""Exercise the actual calibration recorder and its compiled-out default."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
from check_usb_lifecycle import function

ROOT = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
if not compiler:
    raise SystemExit('A host C compiler is required.')
with tempfile.TemporaryDirectory(prefix='nmedia-battery-calibration-') as temporary:
    for enabled, load in ((0, 0), (1, 0), (1, 1)):
        executable = Path(temporary) / f'calibration-{enabled}-{load}.exe'
        subprocess.run([compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-DNDMEDIA_BATTERY_CALIBRATION=' + str(enabled),
                        '-DNDVIDEO_BATTERY_TIMINGS=' + str(enabled),
                        '-DNDVIDEO_BATTERY_ACQUISITION=' + str(enabled),
                        '-DNDMEDIA_BATTERY_LOAD_TEST=' + str(load),
                        '-I' + str(ROOT / 'src/platform'),
                        str(ROOT / 'tools/tests/media/battery_calibration.c'),
                        str(ROOT / 'src/platform/battery_calibration.c'), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=10)
    main_source = (ROOT / 'src/player/main.c').read_text()
    fixture = (ROOT / 'tools/tests/media/battery_finish.c').read_text().replace(
        '/* PRODUCTION_FINISH */', function(main_source, 'save_battery_calibration_report'))
    c = Path(temporary) / 'finish.c'
    c.write_text(fixture)
    for load in (0, 1):
        executable = Path(temporary) / f'finish-{load}.exe'
        subprocess.run([compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-DNDMEDIA_BATTERY_LOAD_TEST=' + str(load), str(c), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=10)
subprocess.run([sys.executable, str(ROOT / 'tools/tests/test_battery_calibration_fit.py')], check=True)
