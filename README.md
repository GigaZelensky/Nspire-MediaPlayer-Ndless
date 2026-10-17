# nMedia Player

A native media player for the **TI-Nspire CX, CX II and CX II-T**, powered by Ndless.
Watch videos with subtitles, play ordinary music files, browse calculator or USB
storage, and listen through USB headphones, speakers or a DAC.

## Download and install

Get the player from [Releases](https://github.com/GigaZelensky/Nspire-MediaPlayer-Ndless/releases/latest).
There are two downloads, each installed as a single calculator file:

| Player | Includes |
| --- | --- |
| **Full: `_nMedia.tns`** | H.264, MPEG-4, HEVC, AV1; PCM/IMA WAV, MP3, FLAC, Vorbis, AAC, Opus; USB storage/audio, covers and visualizations |
| **Video Lite: `_nMedia-lite.tns`** | Local H.264 video, subtitles, seeking, resume and encryption; no audio or USB |

1. Install Ndless for your calculator's OS version.
2. Create an `nMedia` folder on the calculator and copy the chosen player into it.
3. Put media beside the app or inside its subfolders, then open the player.

Videos need the included encoder to become `.nvp.tns` files. Music plays directly
from ordinary supported files; on calculator storage, add `.tns` to the filename
(`song.opus.tns`, for example). The encoded audio bytes stay unchanged.
JPEG/PNG album covers can also use a `.tns` suffix locally.

The Full build carries its decoders inside the app and loads the needed modules
on demand. There are no separate codec files to install. Known video codecs
missing from a Lite/custom build appear in red in the library and show a notice
when opened. Lite plays movies with soundtracks silently.

## Music, video and visualizations

| Music and album art | Vortex | Prism |
| --- | --- | --- |
| ![Music player with album cover and transport controls](examples/screenshots/music-player.png) | ![Vortex music visualization](examples/screenshots/music-vortex.png) | ![Prism music visualization](examples/screenshots/music-prism.png) |

| Video controls | Live-action | Subtitles |
| --- | --- | --- |
| ![Playback and seek preview](examples/screenshots/ui-overlay.png) | ![Live-action playback](examples/screenshots/live-action.png) | ![Selectable subtitles](examples/screenshots/subtitles.png) |

Screenshots are host-rendered at native 320×240 using the player C drawing
routines. They illustrate the interface, not hardware performance.
[More screenshots and capture details](examples/README.md).

The music screen includes tags, file information, a draggable timeline, next/previous
previews, play-once/repeat/auto-next modes, expandable album art and four reactive
visualizations: Flow, Vortex, Prism and Pulse. They analyze the decoded PCM already
in memory, with no extra decoder or storage reads. Controls fade during visualizations.
Accented filenames/tags display as plain Latin text while original paths stay intact.

Video includes frame stepping, seek previews, resume history, text subtitle tracks,
five subtitle fonts, fit/fill/stretch/1:1 scaling, speed controls and encrypted files.
Themes are **Amber, Blue, Green and Red**. The pointer, animated controls, volume
drawer, brightness and night filter work across music and video.

## USB storage and sound

The calculator has no built-in audio output. Connect a compatible USB headset,
speaker or DAC through a **Mini-USB OTG adapter**. A passive 3.5 mm adapter without
a USB audio device cannot provide sound. Use a compatible USB hub for simultaneous
storage and audio.

USB libraries are read-only and support FAT16, FAT32 and exFAT, including supported
partitioned drives. Put media in **`nMedia` at the drive root**:

```text
nMedia/
  Movies/
    episode.nvp.tns
  Music/
    Album/
      01 - Song.opus
      cover.jpg
```

An older `ndvideo` folder is still accepted if `nMedia` is absent. Drives attach
automatically; `U` also rescans. Folder navigation, separate file sizes and hover
details work on both storage sources. During music playback, a lost connection
waits for USB recovery and resumes from the saved position; Esc cancels.

The audio host recognizes compatible UAC1/UAC2 PCM interfaces, including supported
devices behind hubs. Hardware tests use a CX II-T with a HyperX Cloud III USB adapter
and a hub's built-in USB DAC. Original CX video/storage has been tested, but CX audio
and every USB audio/hub combination have not. Audio devices that need proprietary
drivers or unsupported sample formats may not work.

## Encode a video with sound

Download/clone the **whole repository**, rather than just one Python script: the
encoder imports sibling helpers such as `av1_stream.py` and `audio_track.py`.
Use Python 3.10+ and install:

```sh
python -m pip install imageio-ffmpeg numpy pillow
```

H.264 plus Opus at 128 kbps, retaining the source frame rate:

```sh
python tools/encode_ndless_video.py "input.mkv" --output "movie.nvp.tns" --codec h264 --fps source --audio --audio-codec opus --audio-bitrate 128
```

For a smaller/slower calculator, reduce `--fps` or dimensions. H.264 generally
decodes faster than HEVC/AV1; file size alone does not predict playback speed.
The default without an explicit `--fps` is 12 FPS. Video-only encoding is the
default. `--audio` alone uses IMA; choose Opus/MP3/AAC/Vorbis for compressed audio.

To inspect soundtrack languages and titles before encoding:

```sh
python tools/encode_ndless_video.py "input.mkv" --list-audio-tracks
```

Add `--audio-track 1` to select the second source soundtrack (indices start at 0),
or `--audio-track ask` for a prompt. When multiple tracks exist, a terminal prompts
if no index was supplied; automated commands default to track 0. The output currently
contains **one selected soundtrack**. Add `--subtitle embedded` for text subtitles,
and repeat `--subtitle-track N` to keep specific subtitle tracks.

`--bitrate-kbps N --two-pass` targets the video size; `--audio-bitrate N` separately
controls compressed audio. Total size is roughly `(video kbps + audio kbps) ×
duration seconds / 8`, in kilobytes, plus container overhead. Choosing a smaller
source download does not by itself reduce the encoded result.

[Detailed encoding options, subtitle burning, encryption and examples](docs/encoding.md).
The current player accepts NVP versions 9–13; version 13 adds synchronized audio.
Ordinary MP4/MKV video files need conversion. Ordinary music files do not.

## Controls

| Key / pointer | Action |
| --- | --- |
| `Space` | Pause/resume |
| `Left` / `Right`, `4` / `6` | Stack 5-second seeks; hold to repeat |
| `7` / `9`, `Shift+Tab` / `Tab` | Previous / next song in the current folder |
| `Up` / `Down`, `8` / `2` | Volume; hold to repeat |
| `Shift` + those vertical keys | Brightness |
| `Ctrl+M` | Mute/unmute |
| `P` / mode button | Play once, repeat this song, continue through folder |
| `V` / view button | Cover, Flow, Vortex, Prism, Pulse |
| `I` / Info | Tags, filename, source, size, duration and audio details without pausing |
| `C`, `N`, `Ctrl+Up/Down`, `O` | Theme, night filter/intensity, processor settings |
| Timeline / cover | Drag to seek / expand cover; click or Esc closes the cover |
| `Esc` | Close the overlay, cancel a drag or pending seek, or return to the picker |


The timeline supports clicks and dragging. Previous restarts the current song
after three seconds; otherwise it selects the previous track. Click a cover to
expand it and click again or press Esc to close it, without pausing the music.
Touch the middle of the right edge to reveal the volume drawer; drag its knob
or click the speaker to mute. Volume uses a curve for finer control at low levels.

In video, `T` changes subtitle track, `F` changes subtitle font, `+`/`-` changes
subtitle size, `/` changes scale, and `Catalog` opens the controls guide.
`O` opens processor settings. Overclock stability depends on the calculator and
workload; a brief clock benchmark does not establish long-term stability.
[Complete controls and power behavior](docs/controls.md).

During music, idle dim/off timers and the On key can turn the display off while
sound continues. A key/touch wakes it without also triggering a control. Music
does not suspend the CPU while audio is playing. Paused video and the library
can enter standby.

## Quiet defaults and diagnostics

Normal playback and successful USB connections write **no diagnostic logs**.
History/settings are saved in `ndvideo-data` beside the app; that legacy folder
name is retained for compatibility and hidden from the library.

Real failures save a report. Automatic reports start a fresh complete record
after their file reaches 64 KiB, limiting repeated-error growth. They are not
continuous frame recordings. `D` explicitly enables performance recording;
hold `D` while pressing `U` to capture USB setup. `M` shows playback metrics.
Recording adds overhead, so compare normal playback with recording disabled.
Leave the app normally to save an explicitly requested capture.

## Build the player

Use the Ndless SDK, ARM GCC toolchain, make/bash and Python with `pyelftools`.
The SDK libraries must be built first; GitHub Actions contains the complete setup.

```sh
make PROFILE=full       # dist/_nMedia.tns
make PROFILE=lite       # dist/_nMedia-lite.tns
make release            # packages those two builds, symbols, licenses and checksums
```

Custom builds select exactly the codecs/features needed:

```sh
make PROFILE=custom CODECS="h264 mpeg4" AUDIO_CODECS="mp3 opus" USB_STORAGE=1 ARTWORK=1 VISUALIZATIONS=0
make PROFILE=custom CODECS="" AUDIO_CODECS="mp3 opus" USB_STORAGE=0 ARTWORK=0 VISUALIZATIONS=0
```

Video choices: `h264 mpeg4 hevc av1`. Audio choices: `pcm ima mp3 flac vorbis aac opus`.
`USB_STORAGE`, `ARTWORK` and `VISUALIZATIONS` are 0/1 switches. An empty
`AUDIO_CODECS=""` removes audio, including the USB audio implementation; its artwork
and visualizations must be off. `CODEC_MODULES` and `AUDIO_MODULES` control on-demand
versus static decoder linking. Selected modules are always bundled inside the app.
USB audio can still work with `USB_STORAGE=0` when audio codecs are enabled.

Custom output is `_nMedia-custom.tns`; archive each custom result separately.
Each build writes a JSON feature manifest. Build objects are isolated by codec
and feature selection. Official releases contain Full and Lite rather than every
combination. [Architecture and validation](docs/media-development.md).

## Source and licenses

`src/player` contains the library/UI; `src/audio` contains audio decoding and USB
output; `src/movie` handles NVP; `src/storage` handles calculator/USB reads; and
`src/codecs` contains video adapters and decoder sources.

GPL-3.0 project licensing and bundled component terms are in [LICENSE](LICENSE)
and [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES). Release downloads include matching
symbols, feature manifests, checksums and third-party license texts. Example media
and album artwork retain their original rights.
