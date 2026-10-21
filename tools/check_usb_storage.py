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
from check_usb_lifecycle import function

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
        common = [compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-I" + str(ROOT / "src")]
        run(*common, "-I" + str(SRC), TEST / "registers.c", "-o", work / "registers.exe")
        run(work / "registers.exe")
        run(*common, "-I" + str(ROOT / "src/platform"), TEST / "cpu_alias.c", "-o", work / "cpu_alias.exe")
        run(work / "cpu_alias.exe")
        run(*common, "-I" + str(SRC), TEST / "autoconnect.c", "-o", work / "autoconnect.exe")
        run(work / "autoconnect.exe")
        run(*common, "-I" + str(SRC), TEST / "transport.c", SRC / "usb_transport.c", SRC / "usb_scsi.c", "-o", work / "transport.exe")
        run(work / "transport.exe")
        # Compare actual production disk I/O with its previous per-poll yield
        # under a fixed foreground cadence, without a calculator or USB device.
        source = (SRC / "usb_volume.c").read_text()
        extractable = source.replace("DRESULT disk_read", "int disk_read")
        actual = function(extractable, "disk_read_impl").replace(
            "int disk_read_impl", "DRESULT disk_read_impl", 1)
        historical = actual.replace("disk_read_impl", "legacy_disk_read").replace(
            "(void)ndvideo_usb_fs_poll();", "usb_fs_yield();")
        assert historical != actual
        read_fixture = (TEST / "read_budget.c").read_text().replace("/* PRODUCTION_DISK_READ */",
            actual + "\n" + function(extractable, "disk_read").replace(
                "int disk_read", "DRESULT disk_read", 1)).replace("/* LEGACY_DISK_READ */", historical)
        (work / "read_budget.c").write_text(read_fixture)
        run(*common, "-I" + str(SRC), work / "read_budget.c", TEST / "fibers.c",
            SRC / "usb_fs_worker.c", "-o", work / "read_budget.exe")
        run(work / "read_budget.exe")
        run(*common, "-I" + str(fixture), TEST / "fixture.c", fixture / "ff.c", fixture / "ffunicode.c", "-o", work / "fixture.exe")
        for enabled in (0, 1):
            run(*common, "-DNDVIDEO_USB_TIMINGS=" + str(enabled), "-I" + str(SRC),
                TEST / "filesystem.c", TEST / "fibers.c", SRC / "usb_volume.c", SRC / "usb_fs_worker.c",
                SRC / "usb_phase_timing.c", SRC / "fatfs/ff.c", SRC / "fatfs/ffunicode.c",
                "-o", work / ("filesystem" + str(enabled) + ".exe"))
        for folder in ("nMedia", "ndvideo"):
            for kind in range(4):
                image = work / "drive.img"
                run(work / "fixture.exe", kind, image, folder)
                for enabled in (0, 1):
                    run(work / ("filesystem" + str(enabled) + ".exe"), image)
        print("PASS: USB transport and FAT16/FAT32/exFAT/GPT regressions.")


if __name__ == "__main__":
    main()
