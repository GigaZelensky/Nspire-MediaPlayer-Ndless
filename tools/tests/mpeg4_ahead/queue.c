#include "shim.h"
#include <assert.h>

#define MAX_FRAMES 256U
#define CHUNK_FRAMES 8U
static unsigned width, height, frame_count, chunk_count, allocations, decoded, resets;
static uint64_t ticks;
static bool blocked_chunk, fail_once, deny_memory;
static char error_text[192];
static uint16_t *reference;
static uint8_t *chunks[MAX_FRAMES];
static uint32_t offsets[MAX_FRAMES][CHUNK_FRAMES], sizes[MAX_FRAMES];
static ChunkIndexEntry index_entries[MAX_FRAMES];
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #value, error_text); exit(1); } } while (0)
uint64_t monotonic_clock_now_ticks(void) { return ++ticks; }
uint32_t monotonic_clock_ticks_per_second(void) { return TIMER_TICKS_PER_SEC; }
bool playback_capture_active(const Movie *m) { (void)m; return true; }
void playback_capture_stage(const Movie *m, unsigned s, uint64_t a, uint64_t b) { (void)m; (void)s; CHECK(b >= a); }
size_t playback_capture_storage_bytes(void) { return 0; }
void native_runtime_stats_snapshot(NativeRuntimeStats *s) {
    memset(s, 0, sizeof(*s)); s->valid_flags = NATIVE_RUNTIME_STATS_DYNAMIC_POOL;
    s->dynamic_pool_available_bytes = deny_memory ? 1000U : 32U * 1024U * 1024U;
}
size_t movie_lookahead_storage_bytes(const Movie *m) { return m->chunk_size; }
size_t movie_lookahead_storage_bound(const Movie *m) { (void)m; return 65536U; }
void *player_malloc_aligned(size_t n, size_t alignment, uint8_t **allocation) {
    *allocation = malloc(n + alignment); CHECK(*allocation); ++allocations;
    return (void *)(((uintptr_t)*allocation + alignment - 1U) & ~(uintptr_t)(alignment - 1U));
}
void player_free_aligned(void *p, uint8_t *allocation) { CHECK(p && allocation && allocations); --allocations; free(allocation); }
void player_copy_maybe_fast(void *d, const void *s, size_t n) { memcpy(d, s, n); ticks += 100; }
void debug_tracef(const char *fmt, ...) { (void)fmt; }
void debug_failf(const char *fmt, ...) { va_list args; va_start(args, fmt); vsnprintf(error_text, sizeof(error_text), fmt, args); va_end(args); }
const char *debug_last_error(void) { return error_text; }
bool reset_mpeg4_decoder(Movie *m) {
    ++resets; memset(&m->mpeg4.picture, 0, sizeof(m->mpeg4.picture));
    m->mpeg4.decoder_failed = false; m->mpeg4.discontinuity = false;
    return mpeg4_xvid_reset(&m->mpeg4.decoder, (int)width, (int)height);
}
bool reset_h264_decoder(Movie *m) { (void)m; CHECK(false); return false; }
bool blit_h264_picture_rows_to_target(Movie *m, const uint8_t *p, uint16_t *d, size_t pitch, size_t first, size_t rows, bool *f) {
    (void)m; (void)p; (void)d; (void)pitch; (void)first; (void)rows; (void)f; CHECK(false); return false;
}
bool blit_planar_picture_rows(Movie *m, const VideoFrame *p, uint16_t *d, size_t pitch, size_t first, size_t rows, bool *f) {
    CHECK(m->codec == MOVIE_CODEC_MPEG4); *f = false; ticks += 100;
    return mpeg4_xvid_blit_picture_rows(p, d, pitch, first, rows);
}
bool test_decode_picture(void *handle, const uint8_t *data, size_t size, int w, int h, bool discontinuity, VideoFrame *picture) {
    ++decoded; ticks += discontinuity ? 18000U : 10000U;
    if (fail_once) { fail_once = false; return false; }
    return mpeg4_xvid_decode_picture(handle, data, size, w, h, discontinuity, picture);
}
int movie_chunk_for_frame(const Movie *m, uint32_t frame) { return frame < m->header.frame_count ? (int)(frame / CHUNK_FRAMES) : -1; }
bool movie_h264_idr_bounds(const Movie *m, uint32_t frame, uint32_t *first, uint32_t *end) {
    (void)m; (void)frame; (void)first; (void)end; return false;
}
bool load_chunk(Movie *m, int chunk) {
    if (chunk < 0 || (unsigned)chunk >= chunk_count) return false;
    if (chunk == m->loaded_chunk) return true;
    if (m->loaded_chunk < 0 || chunk != m->loaded_chunk + 1) CHECK(reset_mpeg4_decoder(m));
    m->loaded_chunk = chunk; m->chunk_bytes = chunks[chunk]; m->chunk_size = sizes[chunk];
    m->frame_offsets = offsets[chunk]; m->decoded_local_frame = -1;
    m->mpeg4.discontinuity = true; return true;
}
int load_ready_chunk(Movie *m, int chunk) { return blocked_chunk && chunk != m->loaded_chunk ? 0 : load_chunk(m, chunk) ? 1 : -1; }
int reload_h264_chunk_step(Movie *m, int chunk, size_t *offset, unsigned budget) {
    (void)offset; (void)budget; return load_chunk(m, chunk) ? 1 : -1;
}
void invalidate_loaded_chunk_state(Movie *m) { m->loaded_chunk = -1; m->chunk_bytes = NULL; m->frame_offsets = NULL; m->decoded_local_frame = -1; }
void loading_progress_tick(LoadingProgress *p, bool force) { (void)force; ++p->ticks; }
bool decode_to_frame(Movie *m, uint32_t frame) { (void)m; (void)frame; CHECK(false); return false; }

static void read_fixture(const char *path) {
    FILE *f = fopen(path, "rb"); CHECK(f);
    CHECK(fread(&width, 4, 1, f) == 1 && fread(&height, 4, 1, f) == 1 && fread(&frame_count, 4, 1, f) == 1);
    CHECK(frame_count < MAX_FRAMES && frame_count > 32 && !(width & 7U) && !(height & 1U));
    chunk_count = (frame_count + CHUNK_FRAMES - 1U) / CHUNK_FRAMES;
    for (unsigned chunk = 0; chunk < chunk_count; ++chunk) {
        chunks[chunk] = calloc(1, 65536U); CHECK(chunks[chunk]);
        unsigned count = frame_count - chunk * CHUNK_FRAMES; if (count > CHUNK_FRAMES) count = CHUNK_FRAMES;
        index_entries[chunk].first_frame = chunk * CHUNK_FRAMES; index_entries[chunk].frame_count = count;
        for (unsigned local = 0; local < count; ++local) {
            uint32_t size; CHECK(fread(&size, 4, 1, f) == 1 && size && size < 65500U - sizes[chunk]);
            offsets[chunk][local] = sizes[chunk];
            CHECK(fread(chunks[chunk] + sizes[chunk], 1, size, f) == size); sizes[chunk] += size;
        }
    }
    fclose(f);
}
static void check_pixels(Movie *m, unsigned frame) {
    CHECK(m->current_frame == frame);
    CHECK(!memcmp(m->framebuffer, reference + (size_t)frame * width * height, (size_t)width * height * 2U));
}
static void begin(Movie *m) {
    memset(m, 0, sizeof(*m)); m->codec = MOVIE_CODEC_MPEG4; m->loaded_chunk = -1;
    m->mpeg4.planar_supported = true;
    m->header.video_width = width; m->header.video_height = height;
    m->header.frame_count = frame_count; m->header.chunk_count = chunk_count; m->chunk_index = index_entries;
    m->framebuffer = player_malloc_aligned((size_t)width * height * 2U, 32U, &m->framebuffer_allocation);
    CHECK(mpeg4_xvid_create(&m->mpeg4.decoder, (int)width, (int)height) && load_chunk(m, 0));
    size_t size = offsets[0][1]; CHECK(mpeg4_xvid_decode_frame(m->mpeg4.decoder, chunks[0], size, m->framebuffer, width, height, true, true));
    m->decoded_local_frame = 0; m->mpeg4.discontinuity = false;
    CHECK(movie_uses_decode_ahead(m) && video_lookahead_begin(m)); check_pixels(m, 0);
}
static void finish(Movie *m) {
    video_lookahead_destroy(m); mpeg4_xvid_destroy(m->mpeg4.decoder);
    player_free_aligned(m->framebuffer, m->framebuffer_allocation); CHECK(!allocations);
}
static void advance(Movie *m, unsigned frame) {
    int result = video_lookahead_finish_target(m, frame);
    for (unsigned turn = 0; result == 2 && turn < 50; ++turn) result = video_lookahead_finish_target(m, frame);
    if (result != 1) fprintf(stderr, "advance frame=%u chunk=%d local=%d result=%d\n", frame, m->loaded_chunk, m->decoded_local_frame, result);
    CHECK(result == 1); check_pixels(m, frame);
}
static unsigned clamp8(int value) { return value < 0 ? 0U : value > 255 ? 255U : (unsigned)value; }
static void check_color_edges(void) {
    static uint8_t y[64*32], u[32*16], v[32*16];
    static uint16_t pixels[64*32];
    const uint8_t values[] = {0,15,16,17,64,128,235,255};
    for (unsigned i = 0; i < sizeof(y); ++i) y[i] = values[i % 8];
    for (unsigned i = 0; i < sizeof(u); ++i) { u[i] = values[(i / 4) % 8]; v[i] = values[(i / 32) % 8]; }
    VideoFrame picture = {{y,u,v},{64,32,32},64,32,64,32,0,0,0};
    memset(pixels, 0xab, sizeof(pixels));
    CHECK(!mpeg4_xvid_blit_picture_rows(&picture, pixels, 63, 0, 16) && pixels[0] == 0xabab);
    CHECK(mpeg4_xvid_blit_picture_rows(&picture, pixels, 64, 0, 16));
    CHECK(mpeg4_xvid_blit_picture_rows(&picture, pixels, 64, 16, 16));
    for (unsigned row = 0; row < 32; ++row) for (unsigned col = 0; col < 64; ++col) {
        int luma = 298 * ((int)y[row*64+col]-16) + 128;
        int blue = (int)u[(row/2)*32+col/2]-128, red = (int)v[(row/2)*32+col/2]-128;
        unsigned r = clamp8((luma + 409*red) >> 8), g = clamp8((luma - 100*blue - 208*red) >> 8);
        unsigned b = clamp8((luma + 516*blue) >> 8);
        CHECK(pixels[row*64+col] == (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3)));
    }
}
int main(int argc, char **argv) {
    CHECK(argc == 2); read_fixture(argv[1]); CHECK(mpeg4_xvid_global_init(NULL, 0));
    check_color_edges();
    reference = malloc((size_t)frame_count * width * height * 2U); CHECK(reference);
    void *decoder = NULL; CHECK(mpeg4_xvid_create(&decoder, width, height));
    for (unsigned frame = 0; frame < frame_count; ++frame) {
        unsigned chunk = frame / CHUNK_FRAMES, local = frame % CHUNK_FRAMES;
        size_t start = offsets[chunk][local], end = local + 1U < index_entries[chunk].frame_count ? offsets[chunk][local + 1U] : sizes[chunk];
        CHECK(mpeg4_xvid_decode_frame(decoder, chunks[chunk] + start, end-start,
            reference + (size_t)frame * width * height, width, height, true, local == 0));
    }
    mpeg4_xvid_destroy(decoder);
    CHECK(mpeg4_xvid_create(&decoder, width, height));
    uint16_t *planar_rgb = malloc((size_t)width * height * 2U); CHECK(planar_rgb);
    for (unsigned frame = 0; frame < frame_count; ++frame) {
        unsigned chunk = frame / CHUNK_FRAMES, local = frame % CHUNK_FRAMES;
        size_t start = offsets[chunk][local], end = local + 1U < index_entries[chunk].frame_count ? offsets[chunk][local + 1U] : sizes[chunk];
        VideoFrame picture;
        if (!mpeg4_xvid_decode_picture(decoder, chunks[chunk] + start, end-start, width, height, local == 0, &picture)) {
            fprintf(stderr, "planar frame=%u: %s\n", frame, mpeg4_xvid_last_error()); CHECK(false);
        }
        CHECK(mpeg4_xvid_blit_picture_rows(&picture, planar_rgb, width, 0, height));
        CHECK(!memcmp(planar_rgb, reference + (size_t)frame * width * height, (size_t)width * height * 2U));
    }
    mpeg4_xvid_destroy(decoder); free(planar_rgb);
    Movie m; begin(&m);
    unsigned cold_decoded = decoded;
    CHECK(!video_lookahead_step(&m, ticks + 100000U) && decoded == cold_decoded);
    for (unsigned frame = 1; frame <= CHUNK_FRAMES; ++frame) advance(&m, frame);
    unsigned before = decoded; CHECK(!video_lookahead_step(&m, ticks + 1U) && before == decoded);
    for (unsigned step = 0; video_lookahead_queued(&m) < 20 && step < 2000; ++step) {
        CHECK(video_lookahead_step(&m, ticks + 100000U)); check_pixels(&m, CHUNK_FRAMES);
    }
    VideoLookaheadStats stats; video_lookahead_get_stats(&m, &stats);
    CHECK(stats.queued >= 20 && stats.capacity > 5 && stats.packed_ready && stats.packed_frames);
    CHECK(stats.background_frames && !m.mpeg4.picture.plane[0]);
    for (unsigned frame = CHUNK_FRAMES + 1; frame < frame_count; ++frame) {
        advance(&m, frame);
        for (unsigned step = 0; step < 18 && video_lookahead_next_frame(&m) < frame_count; ++step)
            (void)video_lookahead_step(&m, ticks + 100000U);
    }
    finish(&m);
    begin(&m);
    for (unsigned frame = 1; frame <= CHUNK_FRAMES; ++frame) advance(&m, frame);
    for (unsigned step = 0; video_lookahead_queued(&m) < 24 && step < 2000; ++step)
        CHECK(video_lookahead_step(&m, ticks + 100000U));
    int realtime = video_lookahead_finish_realtime_target(&m, 17);
    for (unsigned turn = 0; realtime == 2 && turn < 100; ++turn)
        realtime = video_lookahead_finish_realtime_target(&m, 17);
    CHECK(realtime == 1); check_pixels(&m, 17); advance(&m, 18); finish(&m);
    begin(&m);
    for (unsigned frame = 1; frame <= CHUNK_FRAMES; ++frame) advance(&m, frame);
    blocked_chunk = true;
    for (unsigned step = 0; step < 300; ++step) (void)video_lookahead_step(&m, ticks + 100000U);
    CHECK(video_lookahead_next_frame(&m) == 2U * CHUNK_FRAMES); check_pixels(&m, CHUNK_FRAMES); blocked_chunk = false;
    fail_once = true; CHECK(!video_lookahead_step(&m, ticks + 100000U));
    for (unsigned step = 0; step < 300; ++step) (void)video_lookahead_step(&m, ticks + 100000U);
    video_lookahead_get_stats(&m, &stats); CHECK(stats.failures == 1 && stats.recoveries == 1);
    advance(&m, CHUNK_FRAMES + 1); video_lookahead_cancel(&m); CHECK(!video_lookahead_active(&m) && m.loaded_chunk == -1 && !m.mpeg4.picture.plane[0]);
    CHECK(load_chunk(&m, 0));
    for (unsigned frame = 0; frame <= 3; ++frame) {
        size_t start = offsets[0][frame], end = offsets[0][frame + 1];
        CHECK(mpeg4_xvid_decode_frame(m.mpeg4.decoder, chunks[0] + start, end-start, m.framebuffer, width, height, true, frame == 0));
        m.mpeg4.discontinuity = false;
        m.decoded_local_frame = frame; m.current_frame = frame;
    }
    check_pixels(&m, 3); CHECK(video_lookahead_begin(&m)); advance(&m, 4); finish(&m);
    deny_memory = true; memset(&m, 0, sizeof(m)); m.codec = MOVIE_CODEC_MPEG4;
    m.mpeg4.planar_supported = true; m.mpeg4.decoder = (void *)1;
    m.framebuffer = reference; m.header.video_width = width; m.header.video_height = height;
    m.header.frame_count = frame_count; m.header.chunk_count = chunk_count; m.chunk_index = index_entries;
    CHECK(!video_lookahead_begin(&m)); video_lookahead_destroy(&m); deny_memory = false;
    m.mpeg4.planar_supported = false; CHECK(!movie_uses_decode_ahead(&m));
    for (unsigned chunk = 0; chunk < chunk_count; ++chunk) free(chunks[chunk]); free(reference);
    printf("PASS MPEG4 shared lookahead: %u exact RGB565 frames, >5-frame adaptive YUV queue, owned planes, deadline gates, chunk waits, recovery, cancellation and seek; decoder_calls=%u resets=%u\n", frame_count, decoded, resets);
    return 0;
}
