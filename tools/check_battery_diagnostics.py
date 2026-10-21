#!/usr/bin/env python3
"""Exercise the passive battery observer with bounded synthetic RAM reads."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
from check_usb_lifecycle import function

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/platform/battery_diagnostics.c').read_text()
for name, replacement in (
        ('peripheral_mapping', 'static bool peripheral_mapping(void){return mapping_ok;}'),
        ('adc_write', 'static void adc_write(void *c,uint32_t at,uint32_t v){(void)c;(void)at;(void)v;assert(!"unexpected ADC register write");}'),
        ('adc_barrier', 'static void adc_barrier(void *c){(void)c;}')):
    source = source.replace(function(source, name), replacement)
for name in ('read32', 'read16', 'read8', 'fingerprint'):
    signature = ('uint32_t at,unsigned bytes' if name == 'fingerprint' else 'uint32_t at')
    args = 'at,bytes' if name == 'fingerprint' else 'at'
    source = source.replace(function(source, name),
                            f'static uint32_t {name}({signature}) {{ return test_{name}({args}); }}')
fixture = (ROOT / 'tools/tests/media/battery_diagnostics.c').read_text()
compiler = os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
if not compiler:
    raise SystemExit('A host C compiler is required.')
with tempfile.TemporaryDirectory(prefix='nmedia-battery-observer-') as temporary:
    directory = Path(temporary)
    c = directory / 'battery.c'
    c.write_text(fixture.replace('/* PRODUCTION_BATTERY_DIAGNOSTICS */', source))
    for enabled, acquisition, service in ((0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 0, 1)):
        exe = directory / f'battery-{enabled}-{acquisition}-{service}.exe'
        subprocess.run([compiler, '-std=c99', '-DNDMEDIA_BATTERY_SERVICE='+str(service), '-DNDMEDIA_BATTERY_METER=0', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-DNDVIDEO_BATTERY_TIMINGS=' + str(enabled),
                        '-DNDVIDEO_BATTERY_ACQUISITION=' + str(acquisition),
                        '-I' + str(ROOT / 'src/platform'), str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)
    # Exercise the actual foreground boundary, including a disconnect found
    # by the first pump, and verify acquisition never enters the clock hook.
    music = (ROOT / 'src/player/music_player.c').read_text()
    assert 'battery_diagnostics_acquire(' not in (ROOT / 'src/player/platform_debug.c').read_text()
    c.write_text((ROOT / 'tools/tests/media/battery_maintenance.c').read_text().replace(
        '/* PRODUCTION_MAINTENANCE */', function(music, 'music_battery_maintenance')))
    exe = directory / 'maintenance.exe'
    subprocess.run([compiler, '-std=c99', '-DNDMEDIA_BATTERY_SERVICE=0', '-DNDMEDIA_BATTERY_METER=0', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(ROOT / 'src'), str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
    print('PASS battery foreground boundary: pump, recheck, acquire, pump; failed/stopped audio skips acquisition and timestamps refresh')
