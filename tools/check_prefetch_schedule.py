#!/usr/bin/env python3
"""Exercise real prefetch functions under overloaded and normal playback."""

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(
        r"(?m)^(?:static )?(?:bool|int|void|unsigned|PrefetchedChunk \*)\s*"
        + re.escape(name)
        + r"\(",
        source,
    )
    if not match:
        raise ValueError(name)
    start = match.start()
    brace = source.index("{", match.end())
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def main():
    compiler = os.environ.get("CC") or shutil.which("gcc")
    if not compiler:
        raise SystemExit("A host C compiler is required.")
    source = (ROOT / "src/player/codec_streaming.c").read_text()
    production = (ROOT / "src/player/prefetch_io_policy.h").read_text()
    production += "\n".join(
        function(source, name)
        for name in (
            "prefetch_target_chunk",
            "next_chunk_prefetched_ready",
            "find_prefetch_work_chunk",
            "prefetch_defer_speculative_request",
            "prefetch_wait_step",
            "prefetch_tick",
        )
    )
    fixture = (ROOT / "tools/tests/prefetch_schedule/fixture.c").read_text()
    bridge = "\n".join(
        function((ROOT / path).read_text(), name)
        for path, name in (
            ("src/storage/usb/usb_controller.c", "usb_controller_link_speed"),
            ("src/player/player_reader.c", "player_reader_full_speed_usb"),
            ("src/player/movie_async_io.c", "movie_async_full_speed_usb"),
        )
    )
    bridge_fixture = (ROOT / "tools/tests/prefetch_schedule/bridge.c").read_text()
    with tempfile.TemporaryDirectory(prefix="ndvideo-prefetch-check-") as temporary:
        program = Path(temporary) / "check.c"
        executable = Path(temporary) / ("check.exe" if os.name == "nt" else "check")
        program.write_text(fixture.replace("/* PRODUCTION */", production))
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-function",
                "-DCANDIDATE=1",
                "-DEARLY=1",
                str(program),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)
        program.write_text(bridge_fixture.replace("/* PRODUCTION */", bridge))
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                str(program),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
