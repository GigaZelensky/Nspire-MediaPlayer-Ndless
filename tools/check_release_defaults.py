#!/usr/bin/env python3
"""Verify production logging gates and original-CX RAM fallback."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from check_usb_lifecycle import function

ROOT = Path(__file__).resolve().parents[1]


def main():
    compiler = os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
    if not compiler:
        raise SystemExit('A host C compiler is required.')
    state = (ROOT / 'src/player/player_state.c').read_text()
    assert re.search(r'bool g_debug_logging_enabled\s*=\s*false;', state)
    assert re.search(r'bool g_debug_metrics_enabled\s*=\s*false;', state)
    browser = (ROOT / 'src/player/usb_browser.c').read_text()
    sram = (ROOT / 'src/sram.c').read_text()
    assert re.search(r'#define NDVIDEO_CX_SRAM_EXPERIMENTAL\s+0\b', sram)
    assert re.search(r'#define NDVIDEO_CX_SRAM_SURVEY\s+0\b', sram)
    logging_policy = re.search(r'\.checkpoint_enabled\s*=\s*(.*?),\s*\.fonts', browser, re.S).group(1)
    fixture = (ROOT / 'tools/tests/usb/release_defaults.c').read_text()
    fixture = fixture.replace('/* LOGGING_POLICY */', logging_policy)
    fixture = fixture.replace('/* PRODUCTION_CHECKPOINT */', function(browser, 'connection_checkpoint'))
    fixture = fixture.replace('/* PRODUCTION_EXIT_AUDIT */', function(browser, 'usb_browser_save_exit_audit'))
    fixture = fixture.replace('/* PRODUCTION_SRAM_INIT */', function(sram, 'sram_init'))
    with tempfile.TemporaryDirectory(prefix='ndvideo-release-defaults-') as temporary:
        directory = Path(temporary)
        file, exe = directory / 'defaults.c', directory / ('defaults.exe' if os.name == 'nt' else 'defaults')
        file.write_text(fixture)
        subprocess.run([compiler, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', str(file), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == '__main__':
    main()
