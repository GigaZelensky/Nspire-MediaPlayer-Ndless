#!/usr/bin/env python3
"""Exercise wide movie offsets without allocating a multi-gigabyte fixture."""
from pathlib import Path
import io
import shutil
import struct
import subprocess
import tempfile
import sys
import os

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
TEST = ROOT / "tools/tests/large_files"


class RecordSink:
    def __init__(self):
        self.position = 0
        self.records = {}

    def seek(self, position):
        self.position = position

    def write(self, data):
        self.records[self.position] = bytes(data)
        self.position += len(data)


def main():
    compiler = shutil.which("gcc") or shutil.which("clang")
    if not compiler:
        raise SystemExit("A host C compiler is required.")
    sys.path.insert(0, str(ROOT / "tools"))
    import encode_ndless_video as encoder
    from nve_crypto import EncryptedWriter

    with tempfile.TemporaryDirectory(prefix="ndvideo-large-") as temporary:
        work = Path(temporary)
        base = [compiler, "-std=c99", "-O3", "-Wall", "-Wextra", "-Werror", "-I" + str(SRC)]
        for name, implementation in (("format", "movie/nvp_validation.c"), ("stream", "player/movie_file.c"), ("commands", None)):
            executable = work / (name + ".exe")
            sources = [str(SRC / implementation)] if implementation else []
            subprocess.run([*base, str(TEST / (name + ".c")), *sources, "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

        for version in (11, 12):
            output = io.BytesIO()
            offset = (1 << 36) + 128 if version == 12 else 512
            encoder.write_header(output, version=version, flags=2, max_width=320, max_height=240,
                video_x=0, video_y=0, video_width=320, video_height=180, fps=24,
                chunk_frames=24, frame_count=24, chunk_count=1, subtitle_count=0,
                index_offset=offset, subtitle_offset=0)
            data = output.getvalue()
            assert len(data) == (56 if version == 12 else 48)
            assert int.from_bytes(data[40:48] if version == 12 else data[40:44], "little") == offset
        print("PASS encoder legacy/wide header serialization")

        if os.name == "nt":
            import ctypes
            import msvcrt
            sparse = work / "sparse.nvp.tns"
            offsets = [(1 << 31) + 4096, (1 << 32) + 4096]
            index_offset = (1 << 32) + 16384
            with sparse.open("w+b") as file:
                returned = ctypes.c_ulong()
                control = ctypes.windll.kernel32.DeviceIoControl
                control.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p, ctypes.c_ulong,
                                    ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.c_void_p]
                if not control(msvcrt.get_osfhandle(file.fileno()), 0x900C4, None, 0, None, 0, ctypes.byref(returned), None):
                    raise OSError("Could not mark the owned test file sparse; refusing a dense multi-gigabyte fixture")
                encoder.write_header(file, version=12, flags=0, max_width=320, max_height=240,
                    video_x=0, video_y=0, video_width=320, video_height=180, fps=24,
                    chunk_frames=1, frame_count=2, chunk_count=2, subtitle_count=0,
                    index_offset=index_offset, subtitle_offset=0)
                for number, offset in enumerate(offsets):
                    file.seek(offset)
                    file.write(bytes((i + number * 37) & 255 for i in range(128)))
                file.seek(index_offset)
                for number, offset in enumerate(offsets):
                    file.write(struct.pack("<Q5I", offset, 128, 128, number, 1, 0))
            executable = work / "sparse.exe"
            subprocess.run([*base, str(TEST / "sparse.c"), str(SRC / "player/movie_file.c"),
                            str(SRC / "movie/nvp_validation.c"), "-o", str(executable)], check=True)
            subprocess.run([str(executable), str(sparse)], check=True)

        plain_bytes = (1 << 47) + 271
        writer = EncryptedWriter(work / "header.nve", plain_bytes, 1000, bytearray(b"large-file-test"), 24, 24, 1, 1)
        assert writer.version == 3
        writer.file.close()
        header = (work / "header.nve").read_bytes()
        physical = 128 + plain_bytes + ((plain_bytes + 16383) // 16384) * 32
        offsets = [0, (1 << 31) - 16384, 1 << 31, (1 << 32) - 16384, 1 << 32,
                   (1 << 36) - 16384, 1 << 36, 1 << 37, 1 << 46, 1 << 47]
        sink = RecordSink()
        writer.file = sink
        fixture = work / "crypto-fixture.bin"
        with fixture.open("wb") as file:
            file.write(struct.pack("<Q", physical) + header + struct.pack("<I", len(offsets)))
            for offset in offsets:
                size = min(16384, plain_bytes - offset)
                plaintext = bytes((offset + i ^ (offset >> 32) ^ (i >> 3)) & 255 for i in range(size))
                writer._record(offset // 16384, plaintext)
                ciphertext = sink.records[128 + (offset // 16384) * 16416]
                file.write(struct.pack("<QI", offset, size) + ciphertext + plaintext)
        writer.enc_key = writer.mac_key = b""
        executable = work / "crypto.exe"
        sources = [SRC / "movie/nve_crypto.c", *list((SRC / "crypto/bearssl").glob("*.c"))]
        subprocess.run([*base, str(TEST / "crypto.c"), *map(str, sources), "-o", str(executable)], check=True)
        subprocess.run([str(executable), str(fixture)], check=True)


if __name__ == "__main__":
    main()
