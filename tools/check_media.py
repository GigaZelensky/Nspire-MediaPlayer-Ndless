#!/usr/bin/env python3
"""Check media sections and decoder seeks with short generated A/V fixtures."""
from pathlib import Path
import argparse
import shutil
import subprocess
import sys
import tempfile
from check_usb_lifecycle import function

import imageio_ffmpeg

ROOT = Path(__file__).resolve().parents[1]


def run(arguments):
    subprocess.run(list(map(str, arguments)), cwd=ROOT, check=True)


def check(work):
    from check_player_preferences import check as check_preferences
    check_preferences(work / "preferences")
    from check_audio_controls import check as check_audio_controls
    check_audio_controls(work / "audio-controls")
    from check_space_font import check as check_space_font
    check_space_font(work / "space-font")
    from check_library import check as check_library
    check_library(work / "library")
    from check_music_ui import check as check_music_ui
    check_music_ui(work / "music-ui")
    compiler = shutil.which("gcc") or shutil.which("clang")
    if not compiler:
        raise RuntimeError("A host C compiler is required")
    base = [compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-I", ROOT / "src"]
    tests = ROOT / "tools/tests/media"
    clock_source = (ROOT / "src/player/performance_clock.c").read_text()
    clock = work / "clock.c"
    clock.write_text((tests / "clock.c").read_text().replace(
        "/* PRODUCTION_CLOCK_SAMPLE */", "\n".join(function(clock_source, name) for name in (
            "cx_cpu_hz", "cpu_hz", "cx2_clock_control_idle", "selected_clock_matches",
            "performance_clock_sample"))))
    clock_exe = work / "clock.exe"
    run([*base, "-Werror", clock, "-o", clock_exe])
    run([clock_exe])
    source = (ROOT / "src/audio/audio_playback.c").read_text()
    adapter = (ROOT / "src/audio/movie_audio.c").read_text()
    report = work / "report.c"
    report.write_text((tests / "report.c").read_text().replace(
        "/* PRODUCTION_REPORT */", "\n".join(function(source, name) for name in (
            "report_clock_sample", "save_primary_fault", "save_report", "audio_playback_context", "audio_playback_checkpoint"))+
            "\n"+"\n".join(function(adapter,name) for name in ("context","movie_audio_checkpoint"))))
    for timing in (0, 1):
        report_exe = work / ("report" + str(timing) + ".exe")
        run([*base, "-Werror", "-DNDVIDEO_MEDIA_TIMINGS=" + str(timing), report, "-o", report_exe])
        run([report_exe])
    hub = work / "usb_audio_hub.exe"
    run([*base, "-Werror", "-I", tests, tests / "usb_audio_hub.c",
         ROOT / "src/audio/audio_buffer.c", ROOT / "src/audio/usb_audio_formats.c", "-o", hub])
    run([hub])
    audio_adapter = work / "usb_audio_adapter.exe"
    run([*base, "-Werror", tests / "usb_audio_adapter.c",
         ROOT / "src/audio/usb_audio_formats.c", "-o", audio_adapter])
    run([audio_adapter])
    reconnect = work / "reconnect.c"
    reconnect_source=(ROOT / "src/player/music_reconnect.c").read_text()
    reconnect.write_text((tests / "reconnect.c").read_text().replace(
        "/* PRODUCTION_RECONNECT */", reconnect_source[reconnect_source.index('typedef struct {'):reconnect_source.index('static bool recovery_cancelled')]+
        "\n"+"\n".join(function((ROOT / "src/player/usb_browser.c").read_text(),n) for n in ("render_retry","usb_browser_retry"))+
        "\n"+"\n".join(function(reconnect_source,name) for name in (
            "recovery_cancelled","recovery_draw","music_wait_for_audio"))))
    reconnect_exe=work / "reconnect.exe"
    run([*base,"-Werror",reconnect,"-o",reconnect_exe])
    run([reconnect_exe])
    playback = work / "playback.c"
    source = (ROOT / "src/audio/audio_playback.c").read_text()
    playback_functions="\n".join(function(source,name) for name in (
            "record_elapsed", "capture_transfer_fault", "pump_audio", "audio_playback_pump", "periodic_pump", "audio_playback_suspend", "audio_playback_pause", "audio_playback_seek",
            "audio_playback_retry", "audio_playback_can_reconnect", "audio_playback_cancelled", "audio_playback_active", "audio_playback_source_lost",
            "audio_playback_position", "audio_playback_snapshot", "audio_playback_info", "audio_playback_mute", "tick_audio", "audio_playback_tick"))
    playback_functions+="\n"+"\n".join(function(adapter,name) for name in (
        "movie_audio_suspend","movie_audio_pause","frame_sample","movie_audio_reanchor",
        "movie_audio_tick","movie_audio_source_lost","movie_audio_mute"))
    playback_source=(tests/"playback.c").read_text().replace("/* PRODUCTION_PLAYBACK */",playback_functions)
    playback.write_text(playback_source.replace("/* PRODUCTION_CLOCK */", "\n".join(function(
            (ROOT / "src/player/input_timing_memory.c").read_text(), name) for name in (
                "reset_playback_clock", "reset_playback_timeline"))))
    playback_exe = work / "playback.exe"
    run([*base, "-Werror", playback, "-o", playback_exe])
    run([playback_exe])
    ui = (ROOT / "src/player/picker_ui.c").read_text()
    loop = (ROOT / "src/player/playback_loop.c").read_text()
    start = loop.index("    if (loading_snapshot) {\n        draw_movie_frame_background")
    end = loop.index("    finish_loading_transition", start)
    resume = work / "resume.c"
    resume.write_text((tests / "resume.c").read_text().replace(
        "/* PRODUCTION_LOADING */", function(ui, "loading_progress_init") + "\n" +
        function(ui, "loading_progress_tick")).replace(
        "/* PRODUCTION_AUDIO_START */", loop[start:end]))
    resume_exe = work / "resume.exe"
    run([*base, "-Werror", resume, "-o", resume_exe])
    run([resume_exe])
    container = work / "container.exe"
    run([*base, "-Werror", tests / "container.c", ROOT / "src/movie/nvp_audio.c",
         ROOT / "src/movie/nvp_validation.c", "-o", container])
    run([container])
    decoder = work / "decode.exe"
    from audio_build import build_decoder
    build_decoder(compiler,ROOT,tests/"decode.c",decoder)
    controls=work/"music-controls.exe"
    run([*base,"-Werror",tests/"music_controls.c",ROOT/"src/player/music_controls.c","-o",controls])
    run([controls])
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    source = work / "sync-source.mkv"
    run([ffmpeg, "-nostdin", "-y", "-v", "error", "-f", "lavfi", "-i",
         "testsrc2=size=320x240:rate=24:duration=20", "-f", "lavfi", "-i",
         "aevalsrc=0.12*sin(2*PI*880*t)*lt(mod(t\\,1)\\,0.08):s=44100:d=20",
         "-vf", "drawbox=color=white:t=fill:enable='lt(mod(t,1),0.08)'",
         "-ac", "2", "-c:v", "libx264", "-preset", "ultrafast", "-c:a", "flac", source])
    raw_tracks={}
    from audio_track import FOOTER
    for codec in ("pcm", "ima", "mp3", "flac", "vorbis", "aac", "opus"):
        movie = work / (codec + ".nvp.tns")
        run([sys.executable, ROOT / "tools/encode_ndless_video.py", source,
             "--output", movie, "--codec", "mpeg4", "--fps", "24", "--bitrate-kbps", "200",
             "--start", "2", "--duration", "3", "--audio", "--audio-codec", codec, "--quiet"])
        run([decoder, movie])
        data=movie.read_bytes();footer=FOOTER.unpack(data[-48:])
        raw=work/("native-"+codec+{"aac":".m4a","vorbis":".ogg","opus":".opus","mp3":".mp3","flac":".flac"}.get(codec,".wav"))
        raw.write_bytes(data[footer[2]:footer[2]+footer[3]])
        raw_tracks[codec]=raw
    native=work/"audio-file.exe"
    build_decoder(compiler,ROOT,tests/"audio_file.c",native)
    for raw in raw_tracks.values():
        run([native,raw])
        truncated=work/(raw.name+".truncated")
        truncated.write_bytes(raw.read_bytes()[:16])
        run([native,"--reject",truncated])
    from check_audio_modules import check as check_audio_modules
    check_audio_modules(work/"modules",raw_tracks)
    # Mono/resampling and fractional-frame duration use the same footer clock.
    movie = work / "mono-48k.nvp.tns"
    run([sys.executable, ROOT / "tools/encode_ndless_video.py", source,
         "--output", movie, "--codec", "mpeg4", "--fps", "23.976", "--duration", "3",
         "--audio", "--audio-codec", "ima", "--audio-rate", "48000", "--audio-channels", "1", "--quiet"])
    run([decoder, movie])
    decrypt = work / "decrypt.exe"
    run([*base, "-Werror", tests / "decrypt.c", ROOT / "src/movie/nve_crypto.c",
         *sorted((ROOT / "src/crypto/bearssl").glob("*.c")), "-o", decrypt])
    password = work / "test-password.txt"
    password.write_text("media-test", encoding="ascii")
    encrypted = work / "encrypted.nvp.tns"
    run([sys.executable, ROOT / "tools/encode_ndless_video.py", source,
         "--output", encrypted, "--codec", "mpeg4", "--fps", "24", "--duration", "3",
         "--audio", "--audio-codec", "ima", "--encrypt", "--password-file", password, "--quiet"])
    decrypted = work / "decrypted.nvp.tns"
    run([decrypt, encrypted, decrypted])
    run([decoder, decrypted])
    pilot = work / "Flash and beep.nvp.tns"
    run([sys.executable, ROOT / "tools/encode_ndless_video.py", source,
         "--output", pilot, "--codec", "mpeg4", "--fps", "24", "--bitrate-kbps", "200",
         "--audio", "--audio-codec", "ima", "--quiet"])
    run([decoder, pilot])
    from check_ima_reference import check as check_ima_reference
    check_ima_reference(work / "ima-reference", decoder, work / "ima.nvp.tns")
    from check_mp3_reference import check as check_mp3_reference
    check_mp3_reference(work / "mp3-reference", decoder, work / "mp3.nvp.tns")
    print("PASS generated v13 movies; pilot:", pilot)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Keep fixtures here instead of a temporary directory")
    args = parser.parse_args()
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        check(args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="ndvideo-media-") as temporary:
            check(Path(temporary))


if __name__ == "__main__":
    main()
