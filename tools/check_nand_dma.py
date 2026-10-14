#!/usr/bin/env python3
"""Host checks for CX II DMA channel ownership and read-only NAND recovery."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src/storage"
TEST = ROOT / "tools/tests/nand_dma"


def main():
    compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler:
        raise SystemExit("A host C compiler is required.")
    with tempfile.TemporaryDirectory(prefix="ndvideo-nand-dma-") as temporary:
        for name, source in (("controller", "cx2_dma.c"), ("reader", "spi_nand_reader.c")):
            output = Path(temporary) / (name + ".exe")
            subprocess.run([compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(SRC), str(TEST / (name + ".c")),
                            str(SRC / source), "-o", str(output)], check=True)
            subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    main()
