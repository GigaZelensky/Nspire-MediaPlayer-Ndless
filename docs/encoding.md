# Video encoding

The encoder turns a normal video file into a streamed `.nvp.tns` movie. H.264 is
the default and writes version 10 containers. `--codec hevc` selects H.265;
`--codec av1` selects AV1; `--codec mpeg4` selects MPEG-4 Part 2.
These alternatives use version 11 containers.

### Python Requirements

Use Python 3.10 or newer, with:

```bash
pip install imageio-ffmpeg numpy pillow
```

### Basic Example

```powershell
python .\tools\encode_ndless_video.py "C:\path\to\video.mp4" --output ".\dist\video.nvp.tns"
```

### MPEG-4 Part 2 Example

```powershell
python .\tools\encode_ndless_video.py "C:\path\to\video.mp4" --codec mpeg4 --output ".\dist\video-mpeg4.nvp.tns"
```

### H.265 / HEVC

HEVC trades more decoding work for smaller files at similar image quality.
It is especially useful for animation at modest frame rates; H.264 remains the
better general-purpose choice when smooth playback matters more than file size.
Detailed scenes and higher frame rates can still exceed the calculator's speed,
even with an overclock. Smaller files do not necessarily decode faster.

For example, a 320x180, 16 FPS animation encode went from 23.29 MiB in H.264 to
12.39 MiB in HEVC at similar quality, with no lags reported at 1x on a CX II-T
running at 492 MHz.

```powershell
python .\tools\encode_ndless_video.py "C:\path\to\animation.mkv" `
  --codec hevc --fps 16 --max-width 320 --max-height 180 `
  --stream-profile quality --crf 29.5 --preset veryslow `
  --max-chunk-kib 64 --idr-frames auto --subtitle embedded `
  --output ".\dist\animation-hevc.nvp.tns"
```

This is a starting point, not a fixed quality target. Lower CRF gives higher
quality and larger files; CRF values are not equivalent between H.264 and HEVC.
`--stream-profile fast` reduces decoding work at a detail cost. `balanced` and
`quality` allow smaller coding blocks and spend more encoding time on compression.

HEVC needs FFmpeg with `libx265` and a player version with HEVC support; older
releases cannot play these files. The encoder selects the supported Main 8-bit
settings automatically. Subtitles, encryption, previews and two-pass bitrate
encoding work as usual. Encoding can take considerably longer than H.264.

Use `--idr-frames auto` or a frame count. Oversized groups of frames are shortened
automatically to fit the chunk limit. `--idr-frames byte-auto` and `--level` are
H.264-only.

### AV1

AV1 offers another option for smaller files, at a higher decoding cost. It has
been tested on a CX II-T, but demanding scenes can still stutter even at
492 MHz. Animation at modest frame rates is a useful starting point; H.264
remains the better choice for playback speed.

Use the Full `_nMedia.tns` build for AV1. The
example below uses 16 FPS; `--fps source` keeps the original frame rate.

```powershell
python .\tools\encode_ndless_video.py "C:\path\to\animation.mkv" `
  --codec av1 --fps 16 --max-width 320 --max-height 180 `
  --stream-profile quality --crf 42 --preset slow `
  --max-chunk-kib 64 --idr-frames auto --subtitle embedded `
  --output ".\dist\animation-av1.nvp.tns"
```

AV1 requires FFmpeg with `libaom-av1`. CRF is an integer from 0 to 63; lower
values mean higher quality and larger files. `slow`, `veryslow` and `placebo`
map to encoder speeds 2, 1 and 0. The encoder selects 8-bit 4:2:0, avoids hidden
frames and expensive filters, and keeps chunks independently seekable.
Subtitles, encryption, previews and two-pass bitrate encoding work as usual.
CRF values are not comparable across codecs, and AV1 encoding can take a long
time. Smaller output does not guarantee faster playback.

### Embedded Subtitles

```powershell
python .\tools\encode_ndless_video.py "C:\path\to\video.mkv" --subtitle embedded --output ".\dist\video.nvp.tns"
```

### Burn Subtitles Into Video

```powershell
python .\tools\encode_ndless_video.py "C:\path\to\video.mkv" --subtitle embedded --burn-subtitles --output ".\dist\video.nvp.tns"
```

### Burn Larger Subtitles Into Video

```powershell
python .\tools\encode_ndless_video.py "C:\path\to\video.mkv" --subtitle embedded --burn-subtitles --burn-subtitle-size 1.5 --output ".\dist\video.nvp.tns"
```

`--burn-subtitle-size` scales burned subtitles relative to the default output-safe size. It works for text subtitle burns and embedded bitmap subtitle burns.

### Write a Preview MP4

```powershell
python .\tools\encode_ndless_video.py "C:\path\to\video.mkv" --subtitle embedded --burn-subtitles --preview-mp4 --output ".\dist\video.nvp.tns"
```

`--preview-mp4` also writes a video-only `.preview.mp4` next to the `.nvp.tns` output so you can quickly inspect subtitle burn, framing, and quality on PC before copying the movie to the calculator.

### Preserve Source Framerate

```powershell
python .\tools\encode_ndless_video.py "C:\path\to\video.mkv" --subtitle embedded --fps source --output ".\dist\video.nvp.tns"
```

### Recommended Full-Episode Example

```powershell
python .\tools\encode_ndless_video.py "C:\path\to\video.mkv" --subtitle embedded --output ".\dist\video.nvp.tns" --fps 16 --max-width 320 --max-height 180 --max-chunk-kib 64 --stream-profile quality --crf 14.5 --preset veryslow --level 1.3
```

### Target A Specific Size With 2-Pass ABR

```powershell
python .\tools\encode_ndless_video.py "C:\path\to\video.mkv" --output ".\dist\video.nvp.tns" --fps 16 --max-width 320 --max-height 180 --max-chunk-kib 64 --idr-frames byte-auto --stream-profile quality --bitrate-kbps 140 --two-pass --preset veryslow --level 1.3
```

Use CRF when you want the best quality-per-bit without caring about the exact final size. Use `--bitrate-kbps ... --two-pass` when you need a tighter size target.

Chunks are packed by `--max-chunk-kib`; `64` is a starting point, not a fixed
performance target. Very small chunks can force frequent keyframes, increasing
file size and decoding work, especially with HEVC and AV1. Larger chunks trade
more RAM and longer seek catch-up for fewer keyframes. `--chunk-frames` adds an
optional frame-count ceiling (`0` disables it).
`--idr-frames auto` estimates a fixed keyframe interval from the bitrate and byte cap.

`--idr-frames byte-auto` measures frame sizes and splits oversized GOPs while
leaving the others alone. Files that already fit skip refinement. Analysis is
cached for repeat encodes and nearby bitrate changes; every final chunk is still
checked. `--max-chunk-overshoot-percent` permits small near-misses without adding
more keyframes just to shave off a few bytes.

When `--fps` caps or changes the framerate, the encoder timeline-samples frames and then verifies the encoded frame count against the intended duration. The `.json` sidecar records source fps, target fps, expected frames, actual frames, and drift in milliseconds.

### Main Encoder Options

- `--codec`
- `--output`
- `--subtitle`
- `--burn-subtitles`
- `--burn-subtitle-size`
- `--subtitle-track`
- `--fps`
- `--max-width`
- `--max-height`
- `--active-aspect`
- `--crop`
- `--chunk-frames`
- `--idr-frames`
- `--max-chunk-kib`
- `--max-chunk-overshoot-percent`
- `--crf`
- `--bitrate-kbps`
- `--two-pass`
- `--preset`
- `--level`
- `--stream-profile`
- `--start`
- `--duration`
- `--timeline-drift-tolerance-ms`
- `--preview-mp4`
- `--quiet`

Run `python tools/encode_ndless_video.py --help` for the full CLI.
