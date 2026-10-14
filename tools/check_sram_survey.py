#!/usr/bin/env python3
"""Verify production read-only SRAM capture and diagnostic export."""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    compiler = os.environ.get("CC") or shutil.which("gcc")
    if not compiler:
        raise SystemExit("A host C compiler is required.")
    source = (ROOT / "src/sram.c").read_text()
    start = source.index("#if NDVIDEO_CX_SRAM_SURVEY\n/* Diagnostic snapshots")
    survey = source[start : source.index("void sram_debug(FILE *file)", start)]
    survey = survey.replace(
        '__asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(snapshot->control));',
        "snapshot->control = control_input;",
    ).replace(
        '__asm__ volatile("mrc p15,0,%0,c3,c0,0" : "=r"(snapshot->domain));',
        "snapshot->domain = domain_input;",
    )
    assert "__asm__" not in survey
    fixture = (ROOT / "tools/tests/sram_layout/survey.c").read_text()
    with tempfile.TemporaryDirectory(prefix="ndvideo-sram-survey-") as temporary:
        program = Path(temporary) / "check.c"
        executable = Path(temporary) / ("check.exe" if os.name == "nt" else "check")
        program.write_text(fixture.replace("/* PRODUCTION_SURVEY */", survey))
        subprocess.run(
            [
                compiler,
                "-std=c99",
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
        checkpoint = survey[survey.index("void sram_survey_checkpoint(") :]
        program.write_text(
            "#define NDVIDEO_CX_SRAM_SURVEY 0\n"
            + checkpoint
            + 'int main(void) { sram_survey_checkpoint("disabled"); return 0; }\n'
        )
        subprocess.run(
            [
                compiler,
                "-std=c99",
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
        print("PASS normal-build checkpoint has no hardware/memory dependencies")


if __name__ == "__main__":
    main()
