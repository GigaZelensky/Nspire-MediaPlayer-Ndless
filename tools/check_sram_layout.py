#!/usr/bin/env python3
"""Exercise the production CX SRAM recognizer with read-only native-layout fixtures."""

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    start = source.index("static bool " + name + "(")
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return source[start : end + 1]
    raise ValueError("Unclosed function")


def main():
    compiler = os.environ.get("CC") or shutil.which("gcc")
    if not compiler:
        raise SystemExit("A host C compiler is required.")
    source = (ROOT / "src/sram.c").read_text()
    checker = function(source, "sram_cx_layout")
    checker = checker.replace(
        '__asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(control));',
        "control = control_input;",
    )
    checker = checker.replace(
        '__asm__ volatile("mrc p15,0,%0,c3,c0,0" : "=r"(domain));',
        "domain = domain_input;",
    )
    assert "__asm__" not in checker
    snapshot = source[
        source.index(
            "static struct {", source.index("g_sram_status_message")
        ) : source.index("static void sram_set_status_message")
    ]
    assert "sram_cx_reject" in snapshot
    arrays = []
    for name in (
        "reset_code",
        "stack_offsets",
        "stack_values",
        "startup_code",
        "extents",
    ):
        declaration = re.search(
            r"static const uint32_t " + name + r"\[\] = \{.*?\};", checker, re.DOTALL
        ).group()
        arrays.append(declaration.replace(name, "known_" + name))
    fixture = (ROOT / "tools/tests/sram_layout/fixture.c").read_text()
    fixture = fixture.replace("/* KNOWN_ARRAYS */", "\n".join(arrays)).replace(
        "/* PRODUCTION_CHECKER */", snapshot + checker
    )
    with tempfile.TemporaryDirectory(prefix="ndvideo-cx-sram-check-") as temporary:
        file = Path(temporary) / "check.c"
        executable = Path(temporary) / ("check.exe" if os.name == "nt" else "check")
        file.write_text(fixture)
        subprocess.run(
            [
                compiler,
                "-std=c99",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                str(file),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
