#!/usr/bin/env python3
"""Exercise the optional header failure capture and read-only comparison."""
from pathlib import Path
import os,shutil,subprocess,tempfile,struct
from check_usb_lifecycle import function
ROOT=Path(__file__).resolve().parents[1]
cc=os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
if not cc:raise SystemExit('A host C compiler is required')
with tempfile.TemporaryDirectory(prefix='nmedia-header-diag-') as tmp:
    for enabled in (0,1):
        out=Path(tmp)/str(enabled);out.mkdir();exe=out/'report.exe'
        subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror',
            '-DNDMEDIA_HEADER_DIAGNOSTIC='+str(enabled),'-I'+str(ROOT/'tools/tests/header_diagnostic/stubs'),
            '-I'+str(ROOT/'src'),str(ROOT/'tools/tests/header_diagnostic/report.c'),
            str(ROOT/'src/player/header_diagnostic.c'),str(ROOT/'src/player/error_log.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],cwd=out,check=True,timeout=10)
    raw=function((ROOT/'src/player/storage_read_stream.c').read_text(),'read_raw_at')
    assert raw.index('header_diagnostic_storage(')<raw.index('    drain_reader();')
    # Match the actual failure path: capture exported into the same stream
    # whose ferror/fclose decide the visible saved/failed result.
    platform=(ROOT/'src/player/platform_debug.c').read_text()
    failure=function(platform,'report_movie_open_failure')
    assert failure.index('raw_player_before_native();')<failure.index('header_diagnostic_compare(movie_path);')<failure.index('save_failure_report(')
    resources=(ROOT/'src/player/movie_resources.c').read_text()
    report=function(resources,'debug_dump_report')
    assert 'if (failure && reason && !strcmp(reason,"open-failure"))' in report
    assert report.index('header_diagnostic_write(log_file);')<report.index('ferror(log_file)')<report.index('fclose(log_file)')
    assert 'header_diagnostic_finish' not in (ROOT/'src/player/main.c').read_text()
    # Compile the exact modified loader block against its actual pure format
    # validator. Different wire versions and short reads must stay distinct.
    source=(ROOT/'src/player/movie_open_scan.c').read_text()
    begin=source.index('    uint8_t header_bytes[56];')
    end=source.index('    movie->current_file_pos = -1;',begin)
    fixture=(ROOT/'tools/tests/header_diagnostic/load.c').read_text().replace(
        '    /* PRODUCTION_HEADER_READ */',source[begin:end])
    c=Path(tmp)/'load.c';c.write_text(fixture);headers=Path(tmp)/'headers.bin'
    with headers.open('wb') as f:
        for version in (9,11,12,13):
            # Common header has block_size/chunk_frames before the counts.
            common=struct.pack('<4s12H3I',b'NVP1',version,0,320,240,0,0,320,180,24,1,0,24,24,1,0)
            offsets=struct.pack('<QQ',256,0) if version>=12 else struct.pack('<II',256,0)
            wire=common+offsets
            f.write(wire.ljust(56,b'\0'))
    exe=Path(tmp)/'load.exe'
    subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-DNDMEDIA_HEADER_DIAGNOSTIC=1',
        '-I'+str(ROOT/'src'),str(c),str(ROOT/'src/movie/nvp_validation.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(headers)],check=True,timeout=10)
