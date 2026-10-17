"""Exercise the actual module entry/ops and split codec builds on the host."""
from pathlib import Path
import shutil
import subprocess
from audio_build import build_decoder
from build_codec_modules import sources_for

ROOT=Path(__file__).resolve().parents[1]

def check(work, tracks):
    for name,codec in (("vorbis",4),("aac",5),("opus",6)):
        sources=[ROOT/path for path in sources_for(ROOT,name) if not path.endswith("module_runtime.c")]
        exe=work/name/"module.exe"
        build_decoder(shutil.which("gcc") or shutil.which("clang"),ROOT,
                      ROOT/"tools/tests/media/audio_module.c",exe,sources=sources,
                      defines=("-DNDVIDEO_BUILD_MODULE=1","-DNDVIDEO_BUILD_AUDIO_MODULE=1",
                               "-DNDVIDEO_AUDIO_MODULE_CODEC="+str(codec)))
        subprocess.run([str(exe),str(tracks[name])],check=True,timeout=60)
