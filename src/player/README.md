# Player

This directory contains the Ndless application shell: picker, playback loop,
input handling, UI drawing, history, subtitles, screenshots, and debug overlay.

`player_internal.h` is the private integration header for this layer,
`player_state.c` owns shared state, and codec open/reset/destroy/decode dispatch
goes through `MovieCodecOps`.

- `main.c`: application entry point and top-level SDL/font/SRAM setup
- `platform_debug.c`: platform/display hooks, debug logging, clocks, and path helpers
- `input_timing_memory.c`: pointer input, hover guards, memory stats, and frame timing
- `movie_resources.c`: movie lifetime, fonts, SRAM, and codec global init
- `codec_streaming.c`: chunk loading, prefetch, color conversion, and seek preview decode
- `video_decoder.c`: shared decoder stepping, planar output and seek handling
- `video_lookahead.c`: shared H.264/MPEG-4/HEVC/AV1 decode-ahead queue
- `movie_open_scan.c`: `.nvp` opening, subtitle loading, file scanning, and picker cache model
- `subtitles.c`: subtitle layout, wrapping, caching, and drawing
- `render_primitives.c`: RGB565 drawing primitives, theme palette, panels, text metrics, and video rects
- `playback_ui.c`: playback UI animation, badges, help, progress rendering, and movie rendering
- `picker_ui.c`: row geometry, selection/scroll transitions and loading animation
- `picker_library.c`, `picker_tooltip.c`: library painting and wrapped title/metadata tooltips
- `media_browser.c`, `picker_navigation.c`: folder history, breadcrumbs and navigation controls
- `picker_folder_counts.c`: incremental directory counts without recursive library scans
- `usb_browser.c`: connection UI, retry/automatic detection, diagnostic checkpoints and cleanup
- `native_usb_ui.c`: guarded native display/input ownership, USB scheduler waits and host resume
- `native_usb_ui_resolver.c`: checked native code/layout discovery; unknown layouts are refused
- `player_data.c`: one app-local directory for history, settings and diagnostics
- `native_writer_platform.c`, `native_writer_resolver.c`: portable native writer binding for diagnostic journals
- `nand_dma.c`: validated CX II NAND DMA mapping, cache ownership and transfer statistics
- `history_screenshots.c`: history/theme persistence, screenshots, and seek-bar hover preview
- `picker_loop.c`: interactive movie picker loop
- `resume_prompt.c`: resume prompt drawing and selection
- `playback_loop.c`: movie playback loop
- `playback_seek.c`: resumable seeks and input during catch-up

Common movie state lives in `src/movie/movie.h`. Decoder contexts own the
H.264, HEVC, AV1 and MPEG-4 state; multi-codec builds keep the corresponding
module loaded until every playback and preview context has been released.
H.264, progressive MPEG-4, HEVC and AV1 buffer owned YUV pictures behind a small RGB front queue, using
measured free RAM to set capacity.
MPEG-4 uses the same allocation budget, snapshot ownership, presentation and
seek/cancellation lifecycle. Xvid currently completes a whole frame per decode
call; background work waits until ordinary foreground preparation has measured
that cost, with separate guards for chunk starts. Conversion remains cooperative
and uses Xvid's existing RGB565 kernel to retain its rounding below black.
Interlaced MPEG-4 retains the existing direct RGB path, since the shared YUV
snapshot format describes progressive chroma. Run `python tools/check_mpeg4_lookahead.py`
for actual-decoder output parity and shared-queue lifecycle checks.

Internal playback retains the custom filesystem reader. On CX II, its aligned
page buffer can receive the NAND data phase through FTDMAC020 request line 0.
Small metadata reads, native-scheduler contexts and unavailable channels use PIO.
Invalid DMA mappings report an error.
Each command is entirely DMA or PIO: cancellation retires the channel and resets
SPI before handing storage back; a failed DMA transfer reports an error after
cleanup, without silently retrying that operation through PIO. An active lease prevents the
buffer or filesystem view from being released. Background service returns to
decode work while DMA is pending instead of polling through its whole budget.
Original CX and USB transport retain their own storage backends.

The `nand_dma` diagnostic line reports transfers, fallbacks and recovery without
per-transfer logging or software checksums. The standalone probe's correctness
checks passed on CX II; integrated playback timing still needs hardware testing.
Peripheral request mapping and FIFO pitfalls were informed by girafe's hardware
results. Run `python tools/check_nand_dma.py` for controller and SPI recovery tests.

USB discovery and pipe ownership use the native BSD USBdi stack. Video bulk I/O
uses the EHCI queue in `src/storage/usb`; read-only FatFs runs cooperatively over
that transport. The native OS and player timer states are handed off explicitly
around enumeration. Losing the hardware timebase disables cooperative reads, so
this boundary must remain separate from normal playback.

Host recovery distinguishes a suspended bus from an initialized CX II bus whose
HCD interrupt is not registered. The latter uses the native start/stop methods
only with an empty device tree and validated idle DMA schedules. New attachments
get time to enumerate before recovery runs. Both connection orders support explicit
diagnostic captures. CX II playback has hardware confirmation with a preconnected
drive, automatic insertion and a drive connected through U, including reopening
with the drive still attached. H.264 USB playback on original CX has also been confirmed
at its default clock with SRAM disabled. Newly changed insertion and reopen
paths still need separate original-CX hardware testing. CX uses the TDI EHCI
register map; CX II uses its own
controller map. Native function families and live links are validated at runtime,
including the semaphore and display-ownership differences. The original-CX
queued-copy LCD variant is refused until its ownership path is verified.
Connection checkpoints are opt-in: hold D while pressing U, or enable playback
recording before returning to the library. Normal successful connections and
exits write no diagnostic files. Errors still save reports. Explicit captures
append across retries and launches, so a later attempt does not erase a failure.
The picker debounces the host-adapter ID signal rather than requiring an already
enumerated drive; Cancel suppresses another popup until a new insertion.
The resolver scans mapped code and validates live object links before invoking
native methods; do not replace these checks with OS-version-specific addresses.
Background resolution keeps its scan position between picker frames, with a
short timer budget and a word-count limit if the timer stops. It reads through
the validated current mapping without calling native code or draining metadata
jobs. The native wake waits for storage to become idle before changing mappings
and servicing the OS USB task. Run `python tools/check_usb_lifecycle.py` for
scan bounds, cancellation, driver reuse and background handoff checks.

Hardware acceleration paths
---------------------------
The CX II SPI reader identifies the chip and feature state before selecting
quad (6B) or dual (3B) cache reads. The verified W25N01GW profile accepts EF BA 21
with or without a leading dummy byte; it requires enabled ECC/buffer mode and
no OTP/write latch. No flash features are written. Unknown profiles remain
single-lane, with a diagnostic reason. Metadata below the DMA threshold, native-scheduler contexts and
unavailable channels retain PIO; invalid mappings and transfer faults propagate as errors.
`nand_read_path` reports the selected profile and actual command counts.

USB BOT read commands carry up to 16 KiB through four qTD buffer pages. Physical
mapping validation covers the entire arena. Command/status validation, data
toggles, disconnect/cancel cleanup and native schedule restoration are retained.
`usb_read_path` reports the command limit, negotiated speed and packet sizes.
Original CX accepts native full-speed bulk endpoints; CX II uses high speed.

Movie content uses `movie_file.c` and explicit 64-bit seeks; Newlib's small-file
stdio descriptors remain separate for fonts, settings and diagnostics. NVP
versions 9/10/11 parse their 48-byte headers and 24-byte index entries into
the normalized runtime types; version 12 uses 56-byte headers and 28-byte
entries with wide absolute offsets. Chunk/frame payload sizes stay bounded
by available RAM. USB files, FatFs positions and block addresses are 64-bit;
READ(10) remains the ordinary path, with READ CAPACITY(16)/READ(16) used when
the device or requested block range exceeds 32-bit addressing. Wide NVE
version 3 extends record indices and CTR counters without changing the KDF.
`tools/check_large_files.py` covers the coordinate and cipher boundaries
without allocating a multi-gigabyte test file. These paths still need physical
large-file/large-device testing.

`sha_accel.c` uses the native SHA block protocol on CX/CX II, with model-specific
clock gates. Short, multi-block and 32-byte HMAC known-answer checks precede first use and
repeat after a clock change. The codec loader hashes the module's immutable
header, metadata and initialized image before relocation or execution. Its
failure report compares an independent software hash and a second file read;
neither a mismatch nor an engine fault executes unverified code.
Each HMAC/short KDF batch
owns the engine under IRQ/FIQ exclusion and restores the gate/mask before a
yield. Full input blocks are fed as words; the 32-byte PBKDF2 HMAC path reuses
packed pad words and transfers its inner digest directly between hash rounds.
The pad cache is confined to the lease and wiped before restoring interrupts.
Input registers and temporary keys are wiped after use. Record tags are
verified before plaintext publication. Unsupported models use software;
recognized-model mapping, busy-engine, self-test and transfer failures are
explicit errors. `sha_acceleration` reports actual leases/blocks/HMACs/failures,
including `word_blocks` and `short_hmacs` for the optimized paths.
Further performance claims require integrated hardware playback validation;
the standalone benchmarks establish neither a general FPS gain nor exact LCD
readback correctness. Hardware SHA verification of codec modules has also
passed on original CX; encrypted playback needs its own hardware coverage.
Original-CX SRAM allocation is disabled in production;
decoder tables fall back to RAM while OS ownership remains unresolved.
