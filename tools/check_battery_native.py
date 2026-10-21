#!/usr/bin/env python3
"""Read-only battery binding tests; optional exact raw OS carriers as arguments."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("A host C compiler is required")
with tempfile.TemporaryDirectory(prefix="nmedia-battery-native-") as tmp:
    exe = Path(tmp) / "native.exe"
    subprocess.run([compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-D__arm__",
                    "-I" + str(ROOT / "src/platform"), str(ROOT / "tools/tests/media/battery_native.c"),
                    str(ROOT / "src/platform/battery_native.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe), *sys.argv[1:]], check=True, timeout=60)
