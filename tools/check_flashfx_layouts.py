#!/usr/bin/env python3
"""Replay captured CX II unit headers and check conservative tag selection."""
from pathlib import Path
import os,shutil,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
cc=os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
if not cc:raise SystemExit('A host C compiler is required')
with tempfile.TemporaryDirectory(prefix='nmedia-flashfx-layout-') as tmp:
    exe=Path(tmp)/'layout.exe'
    subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'src'),
        str(root/'tools/tests/header_diagnostic/layout.c'),str(root/'src/storage/flashfx_region.c'),
        str(root/'src/storage/flashfx_metadata.c'),str(root/'src/movie/nvp_validation.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
