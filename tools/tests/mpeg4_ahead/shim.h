#ifndef MPEG4_AHEAD_SHIM_H
#define MPEG4_AHEAD_SHIM_H
#define NDVIDEO_PLAYER_INTERNAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "movie/movie.h"
#include "codecs/mpeg4_xvid.h"
#include "player/video_lookahead.h"
#include "player/native_runtime_stats.h"
#include "player/playback_capture_core.h"
#define PLAYER_CACHE_LINE_SIZE 32U
#define TIMER_TICKS_PER_SEC 1000000U
typedef bool (*H264FramePublishPredicate)(Movie *, uint32_t, void *);
typedef bool (*H264DecodedFrameHook)(Movie *, uint32_t, void *);
typedef bool (*VideoDecodePoll)(void *);
typedef struct LoadingProgress { unsigned ticks; } LoadingProgress;
void loading_progress_tick(LoadingProgress *, bool);
bool decode_to_frame(Movie *, uint32_t);
uint64_t monotonic_clock_now_ticks(void);
uint32_t monotonic_clock_ticks_per_second(void);
size_t movie_lookahead_storage_bytes(const Movie *);
size_t movie_lookahead_storage_bound(const Movie *);
void *player_malloc_aligned(size_t, size_t, uint8_t **);
void player_free_aligned(void *, uint8_t *);
void debug_tracef(const char *, ...);
void debug_failf(const char *, ...);
const char *debug_last_error(void);
bool video_decoder_ready(const Movie *);
bool video_decoder_reset(Movie *);
void video_decoder_mark_failed(Movie *);
void video_decoder_release_picture(Movie *);
uint32_t video_decoder_total_units(const Movie *);
uint32_t video_decoder_done_units(const Movie *);
bool video_decoder_pump(Movie *, uint8_t *, size_t, size_t *, unsigned *, unsigned, uint64_t, bool *, bool *, uint8_t **);
bool video_blit_picture_rows(Movie *, const uint8_t *, uint16_t *, size_t, size_t, size_t, bool *);
bool blit_planar_picture_rows(Movie *, const VideoFrame *, uint16_t *, size_t, size_t, size_t, bool *);
bool blit_h264_picture_rows_to_target(Movie *, const uint8_t *, uint16_t *, size_t, size_t, size_t, bool *);
bool reset_mpeg4_decoder(Movie *);
bool reset_h264_decoder(Movie *);
uint32_t h264_incremental_total_mbs(const Movie *, const storage_t *);
bool pump_h264_access_unit(Movie *, storage_t *, uint8_t *, size_t, size_t *, unsigned *, uint32_t, bool, const char *, bool *, bool *, uint8_t **);
bool movie_h264_idr_bounds(const Movie *, uint32_t, uint32_t *, uint32_t *);
int movie_chunk_for_frame(const Movie *, uint32_t);
bool load_chunk(Movie *, int);
int load_ready_chunk(Movie *, int);
int reload_h264_chunk_step(Movie *, int, size_t *, unsigned);
void invalidate_loaded_chunk_state(Movie *);
bool playback_capture_active(const Movie *);
bool movie_uses_planar_decoder(const Movie *);
bool movie_uses_decode_ahead(const Movie *);
unsigned video_decoder_min_units(const Movie *);
void player_copy_maybe_fast(void *, const void *, size_t);
void playback_capture_stage(const Movie *, unsigned, uint64_t, uint64_t);
size_t playback_capture_storage_bytes(void);
bool test_decode_picture(void *, const uint8_t *, size_t, int, int, bool, VideoFrame *);
#endif
