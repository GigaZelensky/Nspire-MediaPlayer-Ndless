#!/usr/bin/env python3
"""Bounded USB diagnostics, including the compiled-out production default."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import re
from check_usb_lifecycle import function

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src/storage/usb"
compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("A host C compiler is required.")
with tempfile.TemporaryDirectory(prefix="nmedia-usb-phase-") as temporary:
    for enabled in (0, 1):
        executable = Path(temporary) / ("phase" + str(enabled) + ".exe")
        subprocess.run([compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(SRC), "-DNDVIDEO_USB_TIMINGS=" + str(enabled),
                        str(ROOT / "tools/tests/usb/phase_timing.c"),
                        str(SRC / "usb_phase_timing.c"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=10)

        # Extract the actual mount job so its new boundaries cannot silently
        # add a label traversal, duplicate I/O or skip an early-error path.
        mount = (SRC / "usb_volume.c").read_text()
        fixture = (ROOT / "tools/tests/usb/mount_timing.c").read_text().replace(
            "/* PRODUCTION_MOUNT_JOB */", function(mount, "mount_job"))
        c = Path(temporary) / ("mount" + str(enabled) + ".c")
        executable = Path(temporary) / ("mount" + str(enabled) + ".exe")
        c.write_text(fixture)
        subprocess.run([compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(SRC), "-DNDVIDEO_USB_TIMINGS=" + str(enabled), str(c),
                        str(SRC / "usb_phase_timing.c"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=10)

with tempfile.TemporaryDirectory(prefix="nmedia-usb-phase-host-") as temporary:
    source = (SRC / "usb_host_session.c").read_text()
    source = re.sub(r"^#include.*\n", "", source, flags=re.M)
    source = source.replace(
        "#define WORD(address) (*(volatile uint32_t *)(uintptr_t)(address))",
        "#define WORD(address) (*test_register(address))")
    source = source.replace(function(source, "barrier"), "static void barrier(void) {}")
    fixture = (ROOT / "tools/tests/usb/host_session.c").read_text().replace(
        "/* PRODUCTION_HOST_SESSION */", source)
    fixture = fixture.replace(
        "int main(void)\n{",
        "static uint32_t phase_now(void){return clock_ms;}\n"
        "int main(void)\n{\n    usb_phase_timing_init(phase_now);")
    at = fixture.rfind("    return 0;")
    fixture = fixture[:at] + """    UsbPhaseTimingStats phase;
    assert(usb_phase_timing_snapshot(USB_PHASE_OWNERSHIP,&phase) && phase.calls>1 && phase.total_ms>0);
    assert(usb_phase_timing_snapshot(USB_PHASE_HOST_WAKE,&phase) && phase.calls>4 && phase.nonzero_results>0);
    assert(usb_phase_timing_snapshot(USB_PHASE_ENUMERATION,&phase) && phase.calls>100);
""" + fixture[at:]
    c, exe = Path(temporary) / "host.c", Path(temporary) / "host.exe"
    c.write_text(fixture)
    subprocess.run([compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-DNDVIDEO_USB_TIMINGS=1", "-I" + str(SRC), str(c),
                    str(SRC / "usb_phase_timing.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)

with tempfile.TemporaryDirectory(prefix="nmedia-usb-queue-") as temporary:
    source = (ROOT / "src/player/native_usb_ui.c").read_text()
    fixture = (ROOT / "tools/tests/usb/queue_timing.c").read_text().replace(
        "/* PRODUCTION_QUEUE_CAPTURE */", "\n".join(function(source, name) for name in (
            "queue_timing_qtd", "queue_timing_walk", "native_usb_ui_queue_capture")))
    c, exe = Path(temporary) / "queue.c", Path(temporary) / "queue.exe"
    c.write_text(fixture)
    subprocess.run([compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-DNDVIDEO_USB_TIMINGS=1", "-I" + str(SRC), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
