#!/usr/bin/env python3
"""Run production MPEG-4 decode and shared lookahead against direct RGB output."""

import os
import re
import shutil
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEST = ROOT / "tools/tests/mpeg4_ahead"


def run(command):
    result = subprocess.run(
        list(map(str, command)), capture_output=True, text=True, check=False
    )
    if result.returncode:
        raise RuntimeError(
            " ".join(map(str, command)) + "\n" + result.stdout + result.stderr
        )
    return result.stdout


def main():
    compiler = os.environ.get("CC") or shutil.which("gcc")
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        import imageio_ffmpeg

        ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    if not compiler or not ffmpeg:
        raise SystemExit("A host C compiler and FFmpeg are required.")
    out = ROOT / "build/checks/mpeg4-ahead" / sys.platform
    out.mkdir(parents=True, exist_ok=True)
    source_list = (
        (ROOT / "Makefile")
        .read_text()
        .split("XVID_DECODER_SRCS =", 1)[1]
        .split("SOURCES :=", 1)[0]
    )
    sources = [
        ROOT / path for path in re.findall(r"src/codecs/xvid/[^\s\\]+\.c", source_list)
    ]
    sources += [
        ROOT / "src/codecs/mpeg4_xvid.c",
        ROOT / "src/player/video_decoder.c",
        ROOT / "src/player/video_lookahead.c",
        TEST / "queue.c",
    ]
    flags = [
        "-std=gnu99",
        "-O2",
        "-g",
        "-fno-strict-aliasing",
        "-ffunction-sections",
        "-fdata-sections",
        "-DARCH_IS_64BIT",
        "-DARCH_IS_GENERIC",
        "-D__unused=__attribute__((unused))",
        "-DXVID_DECODER_ONLY",
        "-DNDVIDEO_XVID_RGB565_ONLY",
        "-DNDVIDEO_XVID_NO_POSTPROC",
        "-DNDVIDEO_WITH_MPEG4=1",
        "-DNDVIDEO_WITH_H264=0",
        "-DNDVIDEO_WITH_HEVC=0",
        "-DNDVIDEO_WITH_AV1=0",
        "-I" + str(TEST),
        "-I" + str(ROOT / "src"),
        "-I" + str(ROOT / "src/codecs"),
        "-I" + str(ROOT / "src/codecs/xvid"),
    ]
    if sys.platform == "win32":
        flags += ["-include", "windows.h"]

    def build(source):
        obj = out / (source.relative_to(ROOT).as_posix().replace("/", "_") + ".o")
        local = flags.copy()
        if source in (
            ROOT / "src/player/video_decoder.c",
            ROOT / "src/player/video_lookahead.c",
        ):
            local += ["-include", str(TEST / "shim.h")]
        if source == ROOT / "src/player/video_decoder.c":
            local += ["-Dmpeg4_xvid_decode_picture=test_decode_picture"]
        run([compiler, *local, "-c", source, "-o", obj])
        return obj

    with ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(build, sources))
    exe = out / ("queue.exe" if sys.platform == "win32" else "queue")
    run([compiler, *objects, "-Wl,--gc-sections", "-lm", "-o", exe])
    raw = out / "moving.m4v"
    run(
        [
            ffmpeg,
            "-v",
            "error",
            "-y",
            "-f",
            "lavfi",
            "-i",
            "testsrc2=size=320x180:rate=30",
            "-frames:v",
            "64",
            "-c:v",
            "mpeg4",
            "-q:v",
            "3",
            "-bf",
            "0",
            "-g",
            "8",
            "-f",
            "m4v",
            raw,
        ]
    )
    stream = raw.read_bytes()
    starts = [match.start() for match in re.finditer(b"\x00\x00\x01\xb6", stream)]
    assert len(starts) == 64
    prefix = stream[: starts[0]]
    fixture = out / "frames.bin"
    with fixture.open("wb") as file:
        file.write(struct.pack("<3I", 320, 180, len(starts)))
        for frame, start in enumerate(starts):
            end = starts[frame + 1] if frame + 1 < len(starts) else len(stream)
            packet = stream[start:end]
            if frame % 8 == 0:
                assert packet[4] >> 6 == 0
                packet = prefix + packet
            file.write(struct.pack("<I", len(packet)) + packet)
    print(run([exe, fixture]), end="")


if __name__ == "__main__":
    main()
