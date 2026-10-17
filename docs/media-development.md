# Media architecture and validation

`AudioPlayback` is shared by ordinary music files and NVP soundtracks. It owns
the bounded PCM ring, source stream and USB output. `movie_audio.c` maps video
frames to sample positions; pause/seek preserves the headset connection. Music
uses the same transport and volume control, with its own cover/visualization UI.

## Decoders and memory

MP3 uses fixed-point libmad through the existing indexed/gapless stream layer.
Vorbis uses integer Tremor, AAC uses fixed-point FAAD2, and Opus uses its fixed-point
backend with Ogg Opus seeking/preroll. WAV/IMA and FLAC use dr_wav/dr_flac.
Vorbis, AAC and Opus are authenticated on-demand modules in Full, sharing the
video bundle loader. Decoder references remain pinned through close; unused audio
modules unload afterward. PCM/IMA, MP3 and FLAC are built into the Full host.

The default PCM reserve is 65,536 frames (256 KiB for stereo PCM16). Artwork
uses a 216×216 RGB565 cache (91 KiB); compressed art and decoder scratch are
released before playback. JPEG row output avoids a full-resolution RGB copy.
Visualizations use 83,264 bytes only when enabled/selected and consume existing
PCM rather than running another decoder. Original CX SRAM remains disabled;
it uses the normal RAM fallback.

## USB ownership

Storage/audio share a host session and coordinated native USB ownership.
Controller takeover occurs after native enumeration/pipe setup, with explicit
timer-domain boundaries. Cleanup restores native ownership and interrupt/timer
state before returning to the OS. Audio reconnect preserves track position and
one cancelable recovery scene. Failed remounts do not destroy the visible library
cache, folder or selection. Native USB capability discovery remains firmware guarded.

## Build selection

`Makefile` selects source families and defines `src/media_config.h` capabilities.
`tools/build_config.py` emits the exact selection. Full, Lite and custom builds
have separate object paths; decoder module signatures include source/header and
compiler inputs. Lite excludes all audio decoders/music UI and disables USB
discovery. Custom builds can omit video entirely or retain only selected audio.
The picker checks known codec metadata before opening a missing decoder.

## Diagnostics

Production defaults disable metrics, verbose capture and media timing reports.
`NDVIDEO_MEDIA_TIMINGS=1` is a developer-only compile option, not a release flag.
Explicit D recording behavior is retained; it costs CPU time. Errors use bounded
complete-record append histories. Healthy close/reconnect must not create logs.

## Checks

Run `python tools/check_media.py` for real decoder EOF/seeks, independent MP3/IMA
references, metadata/artwork bounds, USB audio setup/races, pause/reconnect and UI
behavior. `check_release_defaults.py` checks quiet logging and original-CX SRAM
fallback; `check_release_features.py` checks Full/Lite/custom selections and log
rollover. `check_usb_storage.py` covers nMedia and legacy ndvideo folders on
FAT16/FAT32/exFAT/GPT images. The workflow also checks USB native lifecycle/timers,
NAND DMA, prefetch, shared MPEG-4 lookahead, large files and encrypted compatibility.

Hardware validation has exercised ordinary supported audio files, local/USB
streaming, hub/direct output, full-song completion, seeks, pause/resume and reconnect
on a CX II-T. H.264 with integer MP3 and Opus has held near 24 FPS in user listening
tests with D disabled. Those are observed workloads, not guarantees for all files,
devices, firmware or overclocks. Original CX audio still needs hardware testing.
