"""Verify native PCM precision through the actual decoder, ring and USB packing."""
from pathlib import Path
import argparse
import shutil
import struct
import subprocess
import tempfile

import imageio_ffmpeg
from audio_build import build_decoder

ROOT = Path(__file__).resolve().parents[1]


def check(work):
    from audio_build import audio_sources
    work.mkdir(parents=True, exist_ok=True)
    exe = work / "pcm-precision.exe"
    build_decoder(shutil.which("gcc") or shutil.which("clang"), ROOT,
                  ROOT / "tools/tests/media/pcm_precision.c", exe,
                  sources=[*audio_sources(ROOT), ROOT / "src/audio/usb_audio_formats.c"])
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    for channels in (1, 2):
        for bits in (8, 16, 24, 32):
            limit = 1 << (bits - 1)
            anchors = [0, 1, -1, -limit, limit - 1, 2, -2, limit // 2 + 1]
            samples = [anchors[i % len(anchors)] if i % 11 < 8 else
                       ((i * 2654435761) % (2 * limit)) - limit
                       for i in range(20017 * channels)]
            raw = b"".join((n + (128 if bits == 8 else 0)).to_bytes(bits // 8, "little",
                          signed=bits != 8) for n in samples)
            name = work / f"pcm-{bits}-{channels}.wav"
            fmt = struct.pack("<HHIIHH", 1, channels, 48000, 48000 * channels * bits // 8,
                              channels * bits // 8, bits)
            body = b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(raw)) + raw
            name.write_bytes(b"RIFF" + struct.pack("<I", len(body) + 4) + b"WAVE" + body)
            reference = name.with_suffix(".s32")
            reference.write_bytes(b"".join(struct.pack("<i", n * (1 << (32 - bits))) for n in samples))
            subprocess.run([str(exe), str(name), str(reference), str(bits), str(channels)], check=True)
            if bits in (16, 24):
                flac = name.with_suffix(".flac")
                subprocess.run([ffmpeg, "-nostdin", "-y", "-v", "error", "-i", str(name),
                                "-c:a", "flac", str(flac)], check=True)
                subprocess.run([str(exe), str(flac), str(reference), str(bits), str(channels)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        check(args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="nmedia-pcm-") as directory:
            check(Path(directory))


if __name__ == "__main__":
    main()
