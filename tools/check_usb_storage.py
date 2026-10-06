#!/usr/bin/env python3
"""Host regressions for USB transport and cooperative read-only FAT/exFAT I/O.

Requires a C compiler. Images are generated in a temporary directory; no
physical drive, calculator, SDK or downloaded sample file is used.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src/storage/usb"
TEST = ROOT / "tools/tests/usb"


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler:
        raise SystemExit("A host C compiler (gcc or clang) is required.")
    with tempfile.TemporaryDirectory(prefix="ndvideo-usb-") as temporary:
        work = Path(temporary)
        fixture = work / "fixture-src"
        fixture.mkdir()
        for name in ("ff.c", "ff.h", "ffconf.h", "ffunicode.c", "diskio.h"):
            shutil.copyfile(SRC / "fatfs" / name, fixture / name)
        shutil.copyfile(SRC / "usb_fs_worker.h", work / "usb_fs_worker.h")
        config = (fixture / "ffconf.h").read_text()
        for key, value in (("FF_FS_READONLY", 0), ("FF_USE_MKFS", 1), ("FF_MIN_GPT", 1)):
            config = re.sub(r"(#define\s+" + key + r"\s+)[^\r\n]+", lambda match: match[1] + str(value), config)
        (fixture / "ffconf.h").write_text(config)
        common = [compiler, "-std=c99", "-O2", "-Wall", "-Wextra"]
        run(*common, "-I" + str(SRC), TEST / "transport.c", SRC / "usb_transport.c", SRC / "usb_scsi.c", "-o", work / "transport.exe")
        run(work / "transport.exe")
        run(*common, "-I" + str(fixture), TEST / "fixture.c", fixture / "ff.c", fixture / "ffunicode.c", "-o", work / "fixture.exe")
        run(*common, "-I" + str(SRC), TEST / "filesystem.c", TEST / "fibers.c", SRC / "usb_volume.c", SRC / "usb_fs_worker.c", SRC / "fatfs/ff.c", SRC / "fatfs/ffunicode.c", "-o", work / "filesystem.exe")
        for kind in range(4):
            image = work / "drive.img"
            run(work / "fixture.exe", kind, image)
            run(work / "filesystem.exe", image)
        print("PASS: USB transport and FAT16/FAT32/exFAT/GPT regressions.")


if __name__ == "__main__":
    main()
