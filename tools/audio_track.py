"""Encode and serialize a bounded NVP v13 audio section."""
from dataclasses import dataclass
from pathlib import Path
import math
import shutil
import struct
import subprocess
import tempfile
import imageio_ffmpeg

MEDIA_VERSION = 13
FOOTER = struct.Struct("<8sQQQIIHHI")
CODECS = {"pcm": 0, "ima": 1, "mp3": 2, "flac": 3, "vorbis": 4, "aac": 5, "opus": 6}


@dataclass
class PreparedAudio:
    path: Path
    rate: int
    samples: int
    channels: int
    codec: int
    temporary: tempfile.TemporaryDirectory

    @property
    def bytes(self):
        return self.path.stat().st_size

    def append(self, output, video_bytes):
        if output.tell() != video_bytes:
            raise ValueError("Audio must follow the complete video/subtitle section")
        with self.path.open("rb") as source:
            shutil.copyfileobj(source, output, 65536)
        output.write(FOOTER.pack(b"NVPAUD13", video_bytes, video_bytes, self.bytes,
                                 self.rate, self.samples, self.channels, self.codec, 0))

    def close(self):
        self.temporary.cleanup()


def prepare_audio(source, *, frames, fps_num, fps_den, start=0, track=0,
                  codec="ima", rate=44100, channels=2, bitrate=192):
    if (not math.isfinite(start) or start < 0 or track < 0 or codec not in CODECS or
            rate not in (8000, 16000, 22050, 32000, 44100, 48000, 96000) or
            channels not in (1, 2) or not 32 <= bitrate <= 320 or
            frames <= 0 or fps_num <= 0 or fps_den <= 0):
        raise ValueError("Invalid audio settings or timeline")
    # Ogg Opus presents PCM on the codec's standard 48 kHz output timeline.
    if codec == "opus":
        rate = 48000
    samples = (frames * fps_den * rate + fps_num // 2) // fps_num
    if not 0 < samples <= 0xFFFFFFFF:
        raise ValueError("Audio timeline exceeds the sample-position range")
    temporary = tempfile.TemporaryDirectory(prefix="ndvideo-audio-")
    fmt = {"mp3": "mp3", "flac": "flac", "vorbis": "ogg", "opus": "ogg", "aac": "mp4"}.get(codec,"wav")
    path = Path(temporary.name) / ("track." + ("m4a" if codec=="aac" else fmt))
    encoder = {"pcm": ["-c:a", "pcm_s16le"],
               "ima": ["-c:a", "adpcm_ima_wav"],
               "mp3": ["-c:a", "libmp3lame", "-b:a", f"{bitrate}k"],
               "flac": ["-c:a", "flac", "-sample_fmt", "s16"],
               "vorbis": ["-c:a", "libvorbis", "-b:a", f"{bitrate}k"],
               "opus": ["-c:a", "libopus", "-b:a", f"{bitrate}k"],
               "aac": ["-c:a", "aac", "-b:a", f"{bitrate}k", "-movie_timescale", str(rate),
                       "-movflags", "+faststart"]}[codec]
    command = [imageio_ffmpeg.get_ffmpeg_exe(), "-nostdin", "-y", "-v", "error",
               "-ss", f"{start:.12f}", "-i", str(source), "-map", f"0:a:{track}",
               "-map_metadata", "-1", "-vn", "-sn", "-dn", "-af",
               f"aresample={rate}:async=1:first_pts=0,apad,atrim=end_sample={samples}",
               "-ac", str(channels), "-ar", str(rate), *encoder, "-f", fmt, str(path)]
    try:
        result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=False)
        if result.returncode:
            raise RuntimeError("Audio encode failed: " + result.stderr.decode("utf-8", "replace").strip())
        if path.stat().st_size < 12:
            raise ValueError("Audio encoder produced an empty track")
        return PreparedAudio(path, rate, samples, channels, CODECS[codec], temporary)
    except BaseException:
        temporary.cleanup()
        raise
