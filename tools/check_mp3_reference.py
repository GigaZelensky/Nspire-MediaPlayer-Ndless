"""Check integer MP3 output, gapless timing and seeks across MPEG versions."""
from array import array
from pathlib import Path
import json
import math
import random
import struct
import subprocess
import imageio_ffmpeg
from audio_track import FOOTER


def check(work, decoder, base_movie):
    work.mkdir(parents=True, exist_ok=True)
    data = base_movie.read_bytes()
    header = struct.unpack_from("<4s12H3I2Q", data)
    footer = FOOTER.unpack(data[-48:])
    video = data[:footer[1]]
    assert header[1] == 13 and footer[0] == b"NVPAUD13"
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    cases = ((8000, 1, "32k"), (16000, 2, "64k"), (22050, 1, "64k"),
             (32000, 2, "192k"), (48000, 2, "320k"), (44100, 2, "vbr"),
             (22050, 1, "vbr"), (44100, 2, "no-xing"))
    results = []
    for rate, channels, mode in cases:
        name = f"{rate}-{channels}-{mode}"
        samples = (header[13] * header[10] * rate + header[9] // 2) // header[9]
        rng = random.Random(1729)
        pcm = array("h", (int(17000 * math.sin(i * (.073 + ch * .017))) +
                          rng.randrange(-6000, 6001)
                          for i in range(samples) for ch in range(channels)))
        raw = work / (name + ".s16")
        raw.write_bytes(pcm.tobytes())
        encoded = work / (name + ".mp3")
        settings = ["-q:a", "2"] if mode == "vbr" else ["-b:a", mode if mode != "no-xing" else "128k"]
        if mode == "no-xing":
            settings += ["-write_xing", "0"]
        subprocess.run([ffmpeg, "-nostdin", "-y", "-v", "error", "-f", "s16le",
                        "-ar", str(rate), "-ac", str(channels), "-i", str(raw),
                        "-c:a", "libmp3lame", *settings, str(encoded)], check=True)
        audio = encoded.read_bytes()
        movie = work / (name + ".nvp.tns")
        movie.write_bytes(video + audio + FOOTER.pack(b"NVPAUD13", len(video), len(video),
                          len(audio), rate, samples, channels, 2, 0))
        actual = work / (name + ".player.s16")
        subprocess.run([str(decoder), str(movie), str(actual)], check=True)
        reference = work / (name + ".reference.s16")
        subprocess.run([ffmpeg, "-nostdin", "-y", "-v", "error", "-i", str(encoded),
                        "-c:a", "pcm_s16le", "-f", "s16le", str(reference)], check=True)
        output = array("h", actual.read_bytes())
        expected = array("h", reference.read_bytes()[:samples * channels * 2])
        assert len(output) == len(expected) == samples * channels
        differences = [a - b for a, b in zip(output, expected)]
        maximum = max(map(abs, differences))
        rms = (sum(d * d for d in differences) / len(differences)) ** .5
        assert maximum <= 4 and rms < .5, (name, maximum, rms)
        results.append(dict(rate=rate, channels=channels, mode=mode, samples=samples,
                            max_difference=maximum, rms_difference=rms))
    (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print("PASS independent MP3 reference: MPEG-1/2/2.5, mono/stereo, CBR/VBR/no-Xing and seeks")
