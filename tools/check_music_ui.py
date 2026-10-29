"""Native music interaction, bounded tags/cover decoding and fixed-point visual analysis."""
from pathlib import Path
import argparse
import base64
import io
import re
import shutil
import struct
import subprocess

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def check(out):
    out.mkdir(parents=True, exist_ok=True)
    from check_music_screenshots import check as check_screenshots
    check_screenshots(out / "screenshots")
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "Host C compiler required"
    flags = [compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "src")]
    tests = ROOT / "tools/tests/media"
    for name in ("music_controls", "music_visual"):
        exe = out / (name + ".exe")
        subprocess.run([*flags, str(tests / (name + ".c")),
                        str(ROOT / "src/player" / (name + ".c")), "-lm", "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    effects = out / "music-effects.exe"
    subprocess.run([*flags, str(tests / "music_effects.c"), str(ROOT / "src/player/music_visual.c"),
                    str(ROOT / "src/player/music_effects.c"), "-lm", "-o", str(effects)], check=True)
    subprocess.run([str(effects)], check=True)
    # Compare the silence shortcut to the same production transform with the
    # shortcut disabled, including all presets' evolving feedback state.
    from check_usb_lifecycle import function as extract_function
    visual_source = (ROOT / "src/player/music_visual.c").read_text()
    reference = extract_function(visual_source, "music_visual_analyze")
    assert reference.count("if (!(peak[0] | peak[1]))") == 1
    reference = reference.replace("music_visual_analyze(", "reference_analyze(", 1)
    reference = reference.replace("if (!(peak[0] | peak[1]))", "if (0)", 1)
    silence_source = out / "music-silence.c"
    silence_source.write_text(visual_source + "\n" + reference)
    silence = out / "music-silence.exe"
    subprocess.run([*flags, "-I", str(ROOT / "src/player"), str(tests / "music_silence.c"),
                    str(silence_source), str(ROOT / "src/player/music_effects.c"),
                    "-o", str(silence)], check=True)
    subprocess.run([str(silence)], check=True, timeout=20)
    from check_audio_controls import function
    ui = (ROOT / "src/player/playback_ui.c").read_text()
    music = (ROOT / "src/player/music_ui.c").read_text()
    header = (ROOT / "src/player/music_ui.h").read_text()
    hover_source = (tests / "music_hover.c").read_text().replace(
        "/* PRODUCTION_MUSIC_STATE */", header[header.index("enum"):header.index("void music_ui_open")])
    hover_source = hover_source.replace("/* PRODUCTION_TRANSITIONS */", "\n".join(
        function(ui, name) for name in (
            "ui_ease_out_cubic", "ui_ease_smoothstep", "ui_transition_init",
            "ui_transition_value_with_ease", "ui_transition_value", "ui_transition_update_ex",
            "ui_transition_update", "ui_transition_update_press_ex", "ui_transition_update_press")))
    hover_source = hover_source.replace("/* PRODUCTION_MUSIC_UPDATE */", function(music, "music_ui_update"))
    hover_source = hover_source.replace("/* PRODUCTION_MUSIC_BRIGHTNESS */", "\n".join((
        function(ui, "status_overlay_update_timing"), function(music, "music_ui_status"),
        function(music, "music_ui_brightness"))))
    hover_source = hover_source.replace("/* PRODUCTION_MUSIC_BUTTON */", "\n".join(
        function(music, name) for name in ("music_corner_pixel", "music_button")))
    hover = out / "music-hover.c"
    hover.write_text(hover_source)
    hover_exe = out / "music-hover.exe"
    subprocess.run([*flags, str(hover), str(ROOT / "src/player/music_controls.c"),
                    "-o", str(hover_exe)], check=True)
    subprocess.run([str(hover_exe)], check=True, timeout=10)
    platform = r"""
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef struct {int unused;} SDL_Surface;
typedef struct {bool off,off_fade_active;} DisplayPowerState;
static DisplayPowerState g_display_power_state;
static unsigned wakes,requests,transitions,idles,activities;
static bool idle_off,paused_argument;
static void display_power_restore_animated(DisplayPowerState*s,uint32_t n){(void)n;++wakes;s->off=s->off_fade_active=false;}
static void display_power_request_off(DisplayPowerState*s,bool paused,uint32_t n){(void)n;++requests;paused_argument=paused;s->off_fade_active=true;}
static void display_power_note_activity(DisplayPowerState*s,uint32_t n){(void)s;(void)n;++activities;}
static void display_power_tick_transition(DisplayPowerState*s,uint32_t n){(void)n;++transitions;if(s->off_fade_active){s->off=true;s->off_fade_active=false;}}
static bool display_power_tick_idle(DisplayPowerState*s,SDL_Surface*screen,uint32_t n,bool allow,bool paused){(void)screen;(void)n;assert(allow);++idles;paused_argument=paused;if(idle_off)s->off=true;return s->off;}
"""
    platform += function((ROOT / "src/player/music_player.c").read_text(), "music_screen_power")
    platform += r"""
int main(void){SDL_Surface screen={0};
 assert(!music_screen_power(&screen,0,false,false,false) && !g_display_power_state.off);
 idle_off=true;assert(!music_screen_power(&screen,120000,false,false,false));
 assert(g_display_power_state.off && !paused_argument && !requests && !wakes);
 unsigned before=idles;assert(music_screen_power(&screen,120100,true,false,false));
 assert(!g_display_power_state.off && wakes==1 && idles==before);
 idle_off=false;assert(!music_screen_power(&screen,120200,true,true,false));
 assert(requests==1 && g_display_power_state.off && !paused_argument);
 assert(music_screen_power(&screen,120300,true,false,true));
 assert(!g_display_power_state.off && wakes==2);
 assert(!music_screen_power(&screen,120400,false,false,true) && paused_argument);
 puts("PASS music uses shared idle/backlight policy, On/wake and paused state without invoking audio or CPU standby APIs");
}
"""
    power = out / "music-display.c"; power.write_text(platform)
    power_exe = out / "music-display.exe"
    subprocess.run([*flags, str(power), "-o", str(power_exe)], check=True)
    subprocess.run([str(power_exe)], check=True)
    exe = out / "metadata.exe"
    # Third-party image code is compiled with its normal warning policy.
    image = out / "cover.o"
    subprocess.run([*flags[:-3], "-I", str(ROOT / "src"), "-Wno-error", "-c",
                    str(ROOT / "src/player/cover_image.c"), "-o", str(image)], check=True)
    # The row-output JPEG path must match unmodified stb RGB output exactly.
    reference_source = out / "cover-reference.c"
    reference = (ROOT / "src/player/cover_image.c").read_text()
    reference = re.sub(r"^#define STBI_JPEG_OUTPUT_ROW.*$", "", reference, flags=re.M)
    reference = re.sub(r"bool jpeg = [^;]+;", "bool jpeg = false;", reference)
    reference = reference.replace("cover_image_decode(", "cover_image_decode_reference(")
    reference = reference.replace("8 * 1024 * 1024", "64 * 1024 * 1024")
    reference_source.write_text("#define STB_IMAGE_STATIC\n" + reference)
    reference_object = out / "cover-reference.o"
    subprocess.run([*flags[:-3], "-I", str(ROOT / "src/player"), "-Wno-unused-function",
                    "-c", str(reference_source), "-o", str(reference_object)], check=True)
    subprocess.run([*flags, "-DCOVER_REFERENCE", str(tests / "metadata.c"),
                    str(ROOT / "src/audio/audio_metadata.c"), str(ROOT / "src/player/display_text.c"), str(image),
                    str(reference_object), "-o", str(exe)], check=True)

    image = Image.new("RGB", (64, 48), (80, 120, 200))
    buf = io.BytesIO()
    image.save(buf, format="JPEG")
    jpeg = buf.getvalue()
    buf = io.BytesIO()
    image.save(buf, format="PNG")
    png = buf.getvalue()
    buf = io.BytesIO()
    Image.new("RGB", (800, 800), (80, 120, 200)).save(buf, format="JPEG", progressive=True, subsampling=0)
    progressive = buf.getvalue()
    be = lambda n: struct.pack(">I", n)
    le = lambda n: struct.pack("<I", n)
    sync = lambda n: bytes((n >> 21 & 127, n >> 14 & 127, n >> 7 & 127, n & 127))
    picture = be(3) + be(10) + b"image/jpeg" + be(0) + be(64) + be(48) + be(24) + be(0) + be(len(jpeg)) + jpeg
    values = [b"TITLE=Song", "ARTIST=Joaquín Sabina".encode(), "ALBUM=Física y Química".encode()]
    comment = lambda items: le(4) + b"test" + le(len(items)) + b"".join(le(len(v)) + v for v in items)
    block = lambda kind, data: bytes([kind]) + len(data).to_bytes(3, "big") + data
    flac = b"fLaC" + block(4, comment(values)) + block(0x86, picture)

    def frame(name, data, version):
        return name + (sync if version == 4 else be)(len(data)) + b"\0\0" + data

    def id3(version, art):
        body = frame(b"TIT2", b"\1" + "Song".encode("utf-16"), version)
        body += frame(b"TPE1", b"\0Joaqu\xedn Sabina", version)
        body += frame(b"TALB", b"\3" + "Física y Química".encode(), version)
        body += frame(b"APIC", b"\0image/jpeg\0\3\0" + art, version)
        return b"ID3" + bytes([version, 0, 0]) + sync(len(body)) + body

    def ogg(packet, serial=1, seq=0, flags=0):
        lace = bytes([255] * (len(packet) // 255) + [len(packet) % 255])
        return b"OggS\0" + bytes([flags]) + b"\0" * 8 + le(serial) + le(seq) + b"\0" * 4 + bytes([len(lace)]) + lace + packet

    ogg_comment = comment(values + [b"METADATA_BLOCK_PICTURE=" + base64.b64encode(picture)])
    opus = ogg(b"OpusHead" + b"\0" * 12, flags=2) + ogg(b"OpusTags" + ogg_comment, seq=1)
    vorbis = ogg(b"\1vorbis" + b"\0" * 12, flags=2) + ogg(b"\3vorbis" + ogg_comment, seq=1)
    atom = lambda name, payload: be(8 + len(payload)) + name + payload
    data = lambda v: atom(b"data", b"\0" * 8 + v)
    ilst = atom(b"\xa9nam", data(b"Song")) + atom(b"\xa9ART", data("Joaquín Sabina".encode()))
    ilst += atom(b"\xa9alb", data("Física y Química".encode())) + atom(b"covr", data(jpeg))
    m4a = atom(b"ftyp", b"M4A \0\0\0\0") + atom(b"moov", atom(b"udta", atom(b"meta", b"\0" * 4 + atom(b"ilst", ilst))))
    variations = []
    for name, mode, value, fmt in (("gray-jpeg", "L", 120, "JPEG"),
                                    ("cmyk-jpeg", "CMYK", (40, 100, 80, 10), "JPEG"),
                                    ("rgba-png", "RGBA", (80, 120, 200, 180), "PNG"),
                                    ("16bit-png", "I;16", 30000, "PNG")):
        buffer = io.BytesIO()
        Image.new(mode, (80, 48), value).save(buffer, format=fmt)
        artwork = buffer.getvalue()
        variations.append((name, id3(4, artwork), artwork))
    expected = ["Song", "Joaquín Sabina", "Física y Química"]
    for name, raw, art in (("flac", flac, jpeg), ("id3v23", id3(3, jpeg), jpeg),
                            ("id3v24", id3(4, png), png), ("progressive-800", id3(4, progressive), progressive), ("opus", opus, jpeg),
                            ("vorbis", vorbis, jpeg), ("m4a", m4a, jpeg), *variations):
        source, cover = out / name, out / (name + ".image")
        source.write_bytes(raw)
        tags = subprocess.check_output([str(exe), str(source), str(cover)]).split(b"\0")[:3]
        assert [v.decode() for v in tags] == expected, (name, tags)
        assert cover.read_bytes() == art, name
    # Truncation at nested lengths, malformed tag sizes and absurd atom offsets.
    damaged = [flac[:n] for n in (0, 5, 7, 15, len(flac) - 100)]
    damaged += [id3(3, jpeg)[:n] for n in (10, 20, 35)]
    damaged += [b"fLaC\x86\xff\xff\xff" + b"\0" * 30,
                atom(b"ftyp", b"M4A ") + be(1) + b"moov" + b"\xff" * 8,
                b"ID3\4\0\0\x7f\x7f\x7f\x7f" + b"\0" * 40]
    for index, raw in enumerate(damaged):
        source = out / f"damaged-{index}"
        source.write_bytes(raw)
        subprocess.run([str(exe), str(source)], stdout=subprocess.DEVNULL, check=True)
    oversized = io.BytesIO()
    Image.new("RGB", (2049, 8)).save(oversized, format="PNG")
    too_many_pixels = io.BytesIO()
    Image.new("RGB", (1600, 1600)).save(too_many_pixels, format="JPEG")
    for index, raw in enumerate((b"", jpeg[:20], png[:20], jpeg + b"\0" * (512 * 1024),
                                 oversized.getvalue(), too_many_pixels.getvalue())):
        source = out / f"bad-image-{index}"
        source.write_bytes(raw)
        assert subprocess.run([str(exe), "--image", str(source)]).returncode == 2
    print("PASS ID3v2.3/v2.4, FLAC, Vorbis/Opus picture comments, M4A tags/art, JPEG/PNG thumbnails, exact JPEG row-sink comparison and bounded malformed metadata")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    check(parser.parse_args().output.resolve())
