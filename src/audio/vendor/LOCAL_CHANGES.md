# Local decoder changes

`sources.json` identifies the upstream revisions; these files retain their
original licenses.

Ogg/Tremor, FAAD2, Opus and opusfile C files include a checked-in
`ndvideo_config.h` before their upstream code. The configuration selects integer
decoding, ARM multiply paths where available, no floating-point Opus API and
no network playback. Vorbis uses the full-accuracy path. Ogg's generated types
select 16-bit short, 32-bit int and 64-bit long long, checked at compile time;
this avoids the Ndless SDK's distinct 32-bit long typedef conflicting with
Tremor exponent pointers. The headers/source list and upstream hashes are
recorded in `sources.json`. Decoder math/tables are otherwise unchanged.

Tremor's `os.h` falls back to compiler byte-order constants when strict C99
hides libc's `BYTE_ORDER` names. This fixes generic host compilation on Linux;
the ARM assembly math path and decoder algorithms are unchanged.

`opusfile/src/opusfile.c` fixes a short forward seek while decoded PCM is still
buffered: the buffered portion of the discard is applied to `od_buffer_pos`
immediately, and only the remainder is left in `cur_discard_count`. Upstream
`op_read_native` serves already-buffered PCM before consuming that count on
the next packet. Previously `op_pcm_tell` reported the new position while
the read returned samples from the old one. The native decoder regression
includes the 1024-to-1031 sample transition that exposed this discrepancy.

`libmad/` is a pinned GPL-2.0-or-later decoder subset. Its C files define
`HAVE_CONFIG_H` to use the checked-in embedded `config.h`; no decoder algorithm
or tables are changed. ARM builds select its integer ARM multiply/accumulate
implementation; host checks use 64-bit integer arithmetic. The host configuration
does not enable operating-system threads, files, or asynchronous decoding.

`minimp3_ex.h` has optional `MINIMP3_EX_USER_FIELDS`, `MINIMP3_EX_RESET` and
`MINIMP3_EX_DECODE` hooks. Without hooks its upstream frame backend remains the
default. `mp3_backend.h` uses those hooks for a per-stream libmad state, retaining
minimp3's header parsing, buffering, gapless delay/padding and seek index.
The libmad adapter retains the next header's reservoir lookahead and releases
its allocations on seek/reset/close. Negative backend results become decode
errors; missing initial reservoir data remains a recoverable preroll condition.

The streaming layer also recognizes a seek to logical sample zero before adding
the gapless decoder delay. It resets to the first audio frame and discards that
delay during ordinary decoding, avoiding a full soundtrack index scan at startup.

`dr_wav.h` adds the optional `DRWAV_IMA_UNSIGNED_DIFF(nibble, step)` hook at the
two IMA nibble expansion sites. The upstream separate-shift arithmetic remains
the default when the hook is absent. `decoder_libraries.c` selects
`((2 * magnitude + 1) * step) >> 3`, matching FFmpeg's WAV IMA encoding and the
FFmpeg 7 decoder used for independent reference checks. The sign, predictor
clamp, step-index update, block layout and seek behavior are unchanged.

The previous rounding difference accumulated within blocks and reset at the
next header. The full episode comparison measured up to 463 PCM units of
extra error before the change. Bit-exact reference and hardware listening
checks are tracked separately; matching a reference does not establish the
cause of every audible artifact.
