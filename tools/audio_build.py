"""Build the actual embedded audio decoders for host-side behavioral checks."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import json
import subprocess
import os

VENDOR_INCLUDES = ("ogg/include", "faad2/include", "opus/include", "opusfile/include")


def audio_flags(root, source=None):
    relative = source.relative_to(root).as_posix() if source else ""
    private = ("opus", "opus/silk", "opus/silk/fixed", "opus/celt") if relative.startswith(
        "src/audio/vendor/opus/") else ("faad2/libfaad",) if relative.startswith(
        "src/audio/vendor/faad2/") else ("tremor",) if relative.startswith(
        "src/audio/vendor/tremor/") else ()
    return [arg for directory in (*private, *VENDOR_INCLUDES) for arg in
            ("-I", str(root / "src/audio/vendor" / directory))]


def audio_sources(root):
    return [root / "src/audio/audio_decoder.c", root / "src/audio/audio_buffer.c",
            root / "src/audio/decoder_libraries.c", root / "src/audio/mp3_fixed.c",
            *sorted((root / "src/audio").glob("extra_*.c")),
            *sorted((root / "src/audio/vendor").rglob("*.c"))]


def build_decoder(compiler, root, test, output, *, sources=None, defines=()):
    output.parent.mkdir(parents=True, exist_ok=True)
    cache = output.parent / "audio-objects"
    cache.mkdir(exist_ok=True)
    headers = hashlib.sha256()
    for p in sorted((root / "src/audio").rglob("*")):
        if p.suffix in (".h", ".inc", ".dat"):
            headers.update(p.read_bytes())
    digest = headers.hexdigest()
    flags = [compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-I", str(root / "src"),*defines]
    if os.environ.get("NDVIDEO_EXTRA_AUDIO_TRACE") == "1":
        flags += ["-DNDVIDEO_EXTRA_AUDIO_TRACE=1"]
    def compile_source(source):
        obj = cache / (source.relative_to(root).as_posix().replace("/", "_") + ".o")
        args = [*flags, *audio_flags(root,source), "-c", str(source), "-o", str(obj)]
        sig = dict(command=args, source=hashlib.sha256(source.read_bytes()).hexdigest(), headers=digest)
        signature = obj.with_suffix(".json")
        if not obj.exists() or not signature.exists() or json.loads(signature.read_text()) != sig:
            result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            if result.returncode:
                raise RuntimeError(str(source) + "\n" + result.stdout)
            signature.write_text(json.dumps(sig))
        return obj
    if sources is None:
        sources = audio_sources(root) + [root / "src/movie/nvp_audio.c", root / "src/movie/nvp_validation.c"]
    with ThreadPoolExecutor(max_workers=6) as pool:
        objects = list(pool.map(compile_source, sources))
    args = [*flags[1:], *audio_flags(root), str(test), *map(str, objects), "-lm", "-o", str(output)]
    response = cache / "link.rsp"
    response.write_text(" ".join('"' + arg.replace("\\", "/") + '"' for arg in args))
    subprocess.run([compiler, "@" + str(response)], check=True)
