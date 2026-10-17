#!/usr/bin/env python3
"""Exercise production resident-driver authentication without a USB device."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    # Skip forward declarations; only extract a definition with a body.
    match = re.search(r'(?m)^(?:static )?(?:bool|void|int|uint32_t|uint64_t|size_t) ' + name +
                      r'\([^;{}]*\)\s*(?=\{)', source)
    if not match:
        raise ValueError(name)
    brace = source.index('{', match.end())
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    source = (ROOT / 'src/storage/usb/usb_controller.c').read_text()
    types = source[source.index('typedef struct UsbController'):source.index('unsigned usb_controller_max_read_bytes')]
    # Host pointer width differs from ARM. Exercise the same offsetof-based
    # authentication on host layouts; ARM ABI offsets remain compile-time checked.
    types = re.sub(r'typedef char .*?;', '', types)
    fixture = (ROOT / 'tools/tests/usb/lifecycle.c').read_text()
    compiler = os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
    if not compiler:
        raise SystemExit('A host C compiler is required.')
    with tempfile.TemporaryDirectory(prefix='ndvideo-usb-lifecycle-') as temporary:
        folder = Path(temporary)
        file, exe = folder / 'lifecycle.c', folder / ('lifecycle.exe' if os.name == 'nt' else 'lifecycle')
        file.write_text(fixture.replace('/* PRODUCTION_TYPES */', types).replace(
            '/* PRODUCTION_ATTACHMENTS */',function(source,'select_attachment')+'\n'+function(source,'remember_attachment')).replace(
            '/* PRODUCTION_FINISH */',function(source,'usb_storage_attachment_ready')+'\n'+function(source,'usb_storage_next_candidate')).replace(
            '/* PRODUCTION_REUSE */', function(source, 'reuse_storage_binding')).replace(
            '/* PRODUCTION_AUDIO_CLASSIFICATION */', function(source, 'usb_controller_audio_only')))
        subprocess.run([compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(ROOT / 'src/storage/usb'), str(file), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)
        ui = (ROOT / 'src/player/native_usb_ui.c').read_text()
        file.write_text((ROOT / 'tools/tests/usb/hub_parent.c').read_text().replace(
            '/* PRODUCTION_PARENT */',function(ui,'storage_parent_owner')))
        subprocess.run([compiler,'-std=c99','-O2','-Wall','-Wextra','-Werror',str(file),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,timeout=10)
        bus=(ROOT / 'src/player/native_usb_bus.inc').read_text()
        file.write_text((ROOT / 'tools/tests/usb/device_state.c').read_text().replace(
            '/* PRODUCTION_STATE */',function(bus,'native_usb_ui_bus_device_state')))
        subprocess.run([compiler,'-std=c99','-O2','-Wall','-Wextra','-Werror',
                        '-I'+str(ROOT / 'src/storage/usb'),str(file),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,timeout=10)
        file.write_text((ROOT / 'tools/tests/usb/prepare_steps.c').read_text().replace(
            '/* PRODUCTION_PREPARE */', function(ui, 'native_usb_ui_prepare_step')))
        subprocess.run([compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(file), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)
        subprocess.run([compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(ROOT / 'src/player'),
                        str(ROOT / 'tools/tests/usb/resolver_steps.c'),
                        str(ROOT / 'src/player/native_usb_ui_resolver.c'),
                        '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)
        browser = (ROOT / 'src/player/usb_browser.c').read_text()
        file.write_text((ROOT / 'tools/tests/usb/idle_steps.c').read_text().replace(
            '/* PRODUCTION_IDLE */', function(browser, 'usb_browser_idle_discovery')+'\n'+
            function(browser,'usb_browser_recovery_poll')))
        subprocess.run([compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(file), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)
        file.write_text((ROOT / 'tools/tests/usb/discovery.c').read_text().replace(
            '/* PRODUCTION_DISCOVERY */', function(browser, 'discovery_native')))
        subprocess.run([compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', str(file), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)
        sample = function(browser, 'sample_usb_registers')
        sample = sample.replace('__asm__ volatile("mcr p15,0,%0,c7,c10,4" :: "r"(0) : "memory");', 'test_barrier();')
        assert '__asm__' not in sample
        file.write_text((ROOT / 'tools/tests/usb/insertion.c').read_text().replace(
            '/* PRODUCTION_SAMPLE */', sample).replace(
            '/* PRODUCTION_POLL */', function(browser, 'usb_browser_auto_pending')))
        subprocess.run([compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(ROOT / 'src/storage/usb'), str(file), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == '__main__':
    main()
