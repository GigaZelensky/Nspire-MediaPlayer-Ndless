#include "audio_playback.h"
#include "audio_decoder.h"
#include "usb_audio.h"
#include "storage/usb/usb_host_session.h"
#include "player/player_internal.h"
#include "player/native_usb_ui.h"
#include "player/native_interrupts.h"
#include "player/performance_clock.h"
#include "player/raw_player_io.h"
#include "player/app_task_io.h"
#include "player/player_data.h"
#include "player/usb_browser.h"
#include "storage/usb/usb_storage.h"
#include "storage/usb/usb_controller.h"
#include <errno.h>
#if NDVIDEO_CODEC_MODULES || NDVIDEO_AUDIO_MODULES
#include "codecs/codec_module_api.h"
#endif

struct AudioPlayback {
    MovieReadFile *file;
    AudioBuffer buffer;
    AudioDecoder *decoder;
    AudioDecodeStats stats;
    UsbAudio *usb;
    LoadingProgress *loading;
    SDL_Surface *screen;
    const Fonts *fonts;
    uint64_t offset, bytes, cursor, position;
    unsigned volume, num, den;
    uint32_t pause_max_ms, seek_max_ms, prefill_max_ms, start_max_ms;
    uint32_t last_seek_ms, last_prefill_ms, last_start_ms;
    unsigned restart_count, seek_count;
    bool mute, running, failed, need_seek, native_operation, notice, usb_failed;
    const char *hardware_stage;
    int hardware_result;
    int source_error;
    PerformanceClockSample clock_before_usb, clock_after_usb;
    unsigned clock_samples;
    uint32_t context_position, context_total;
    const char *context_codec;
    char text[160];
    char transfer_fault[384];
    char source_path[MAX_PATH_LEN];
    void (*diagnostics)(FILE *,const void *);
    const void *diagnostics_context;bool diagnostics_error;
};
static AudioPlayback *current_audio;
static unsigned remembered_volume=20;
static bool remembered_mute;


static void record_elapsed(uint32_t *maximum, uint32_t start)
{
    uint32_t elapsed = monotonic_clock_now_ms() - start;
    if (elapsed > *maximum) *maximum = elapsed;
}

static void periodic_pump(void *context)
{
    AudioPlayback *a = context;
    audio_playback_pump(a);
}

static void notice(AudioPlayback *a, const char *text)
{
    snprintf(a->text, sizeof(a->text), "%s", text ? text : "USB audio unavailable");
    a->notice = true;
}
static void capture_transfer_fault(AudioPlayback *a)
{
    a->usb_failed = true;
    /* Teardown uses endpoint-zero requests and can fail independently. Keep
     * the first streaming failure before that cleanup changes the USB trace. */
    if (!*a->transfer_fault)
        snprintf(a->transfer_fault, sizeof(a->transfer_fault), "error=%s last_usb_fault=%s",
                 usb_audio_error(a->usb), usb_audio_transfer_fault());
}

static bool progress(void *context)
{
    AudioPlayback *a = context;
    if (a->loading)
        loading_progress_tick(a->loading, false);
    if (a->loading && (isKeyPressed(KEY_NSPIRE_ESC) ||
        (a->loading->cancelled && a->loading->cancelled(a->loading->context)))) {
        a->source_error=ECANCELED;return false;
    }
    return true;
}
#if NDVIDEO_CODEC_MODULES || NDVIDEO_AUDIO_MODULES
static bool module_progress(void *context,size_t done,size_t total)
{ (void)done;(void)total;return progress(context); }
#endif

static size_t read_input(void *context, void *data, size_t bytes)
{
    AudioPlayback *a = context;
    if (a->loading && !progress(a)) {a->source_error=ECANCELED;return 0;}
    uint64_t available = a->bytes - a->cursor;
    if (bytes > available)
        bytes = (size_t)available;
    if (movie_file_seek(a->file, a->offset + a->cursor)) {
        a->source_error=errno?errno:EIO;
        return 0;
    }
    size_t got = movie_file_read(data, 1, bytes, a->file);
    if (got<bytes) a->source_error=errno?errno:EIO;
    a->cursor += got;
    if (a->loading)
        loading_progress_tick(a->loading, false);
    return got;
}

static bool seek_input(void *context, uint64_t position)
{
    AudioPlayback *a = context;
    if (position > a->bytes)
        return false;
    a->cursor = position;
    return true;
}

static uint64_t tell_input(void *context)
{
    return ((AudioPlayback *)context)->cursor;
}

typedef struct { AudioPlayback *audio; bool closing, ok; } HardwareOperation;


static void hardware_native(void *context)
{
    HardwareOperation *operation = context;
    AudioPlayback *a = operation->audio;
    unsigned saved = native_critical_enter(), kernel = 0;
    bool clock = false, policy = false;
    const volatile uint32_t *counter;
    uint32_t hz;
    if (!operation->closing)
        performance_clock_sample(&a->clock_before_usb);
    a->hardware_stage = "native clock";
    a->hardware_result = -23;
    if (!native_usb_ui_clock(&counter, &hz, progress, a) ||
        !monotonic_clock_enter_native(counter, hz))
        goto done;
    clock = true;
    kernel = TCT_Local_Control_Interrupts(0);
    native_critical_enter();
    policy = true;
    if (operation->closing) {
        a->hardware_stage = "closing headset";
        native_critical_leave(0);
        /* Exit must finish the journal/USB cleanup even while Esc is held. */
        usb_audio_set_progress(NULL,NULL);
        if (a->usb && !usb_audio_close(a->usb))
            goto done;
        a->usb = NULL;
        usb_host_session_release();
        operation->ok = true;
        a->hardware_result = 0;
    } else {
        a->hardware_stage = "native UI reservation";
        if (!native_usb_ui_active())
            usb_browser_reserve_native_ui_order();
        a->hardware_result = native_usb_ui_acquire(progress, a);
        if (a->hardware_result)
            goto done;
        native_critical_leave(0);
        a->hardware_stage = "host lease";
        char message[160] = "Preparing headset connection...";
        if (!usb_host_session_prepare(monotonic_clock_now_ms, progress, a, message,
                                    sizeof(message))) {
            notice(a, message);
            goto done;
        }
        usb_audio_set_route_lookup(native_usb_ui_bus_route, NULL);
        usb_audio_set_state_lookup(native_usb_ui_bus_device_state, NULL);
        usb_audio_set_progress(progress, a);
        usb_audio_prefer_rate(a->buffer.sample_rate);
        const char *status = NULL;
        a->hardware_stage = "headset discovery/configuration";
        a->usb = usb_audio_open(monotonic_clock_now_ms, &status);
        if (!a->usb) {
            notice(a, status);
            usb_host_session_release();
            goto done;
        }
        operation->ok = true;
        a->hardware_stage = "ready";
    }
done:
    native_critical_enter();
    if (!operation->closing) {
        performance_clock_sample(&a->clock_after_usb);
        ++a->clock_samples;
    }
    if (clock)
        monotonic_clock_leave_native();
    if (policy)
        TCT_Local_Control_Interrupts(kernel);
    native_critical_leave(saved);
}

static bool hardware_close(AudioPlayback *a)
{
    if (!a->usb)
        return true;
    raw_player_before_read();
    if (a->running)
        a->position = usb_audio_position(a->usb);
    a->running = false;
    usb_controller_set_periodic_service(NULL, NULL);
    HardwareOperation operation = {a, true, false};
    a->native_operation = true;
    bool mapped = sram_with_native_mapping(hardware_native, &operation);
    a->native_operation = false;
    if (!mapped || !operation.ok)
        return false;
    app_task_io_pause_service(false);
    return true;
}

static void close_with_retry(AudioPlayback *a)
{
    bool previous = true;
    while (!hardware_close(a)) {
        SDL_FillRect(a->screen, NULL, 0);
        draw_ui_label(a->screen, a->fonts, 16, 90, "USB audio is still stopping.");
        draw_ui_label(a->screen, a->fonts, 16, 110, "Esc retries cleanup.");
        present_screen(a->screen);
        for (;;) {
            bool down = isKeyPressed(KEY_NSPIRE_ESC);
            if (down && !previous)
                break;
            previous = down;
            player_delay_ms(16);
        }
        previous = true;
    }
}

static void native_guard(void *context)
{
    AudioPlayback *a = context;
    if (!a || !a->usb || a->native_operation)
        return;
    close_with_retry(a);
}

static bool hardware_open(AudioPlayback *a)
{
    raw_player_before_native();
    HardwareOperation operation = {a, false, false};
    a->native_operation = true;
    bool mapped = sram_with_native_mapping(hardware_native, &operation);
    a->native_operation = false;
    if (!mapped || !operation.ok) {
        if (!a->notice) {
            char message[160];
            snprintf(message, sizeof(message), "Audio setup failed: %s (%d)",
                mapped ? a->hardware_stage : "native mapping", a->hardware_result);
            notice(a, message);
        }
        if (a->usb)
            close_with_retry(a);
        return false;
    }
    usb_controller_set_periodic_service(periodic_pump, a);
    app_task_io_pause_service(true);
    return true;
}


AudioPlayback *audio_playback_open(const char *path, const AudioPlaybackTrack *track, LoadingProgress *loading)
{
    if (!path || !loading || current_audio)
        return NULL;
    AudioPlayback *a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    a->volume = remembered_volume;
    a->mute = remembered_mute;
    snprintf(a->source_path,sizeof(a->source_path),"%s",path);
    a->num = a->den = 1;
    a->loading = loading;
    a->screen = loading->screen;
    a->fonts = loading->fonts;
    a->context_codec = "audio";
    errno = 0;
    a->file = movie_file_open(path);
    if (!a->file) {
        a->source_error = errno ? errno : EIO;
        a->failed = true;
        notice(a, "Audio file could not be opened");
        goto done;
    }
    uint64_t size = movie_file_size(a->file);
    a->offset = track ? track->offset : 0;
    a->bytes = track ? track->bytes : size;
    if (a->offset > size || a->bytes > size - a->offset || a->bytes < 12) {
        a->failed = true;
        notice(a, "Audio file bounds are invalid");
        goto done;
    }
    AudioInput input = {a, read_input, seek_input, tell_input, a->bytes};
    char error[160];
#if NDVIDEO_CODEC_MODULES || NDVIDEO_AUDIO_MODULES
    codec_modules_set_progress(module_progress,a);
#endif
    a->decoder = audio_decoder_open(&a->buffer, input, &a->stats, monotonic_clock_now_ms,
                                    0, error, sizeof(error));
#if NDVIDEO_CODEC_MODULES || NDVIDEO_AUDIO_MODULES
    codec_modules_set_progress(NULL,NULL);
#endif
    if (!a->decoder || (track && (a->buffer.sample_rate != track->rate ||
        a->buffer.channels != track->channels || a->stats.codec != track->codec ||
        !audio_decoder_limit(a->decoder, track->frames)))) {
        a->failed = true;
        notice(a, a->decoder ? "Audio descriptor does not match its stream" : error);
        goto done;
    }
    a->need_seek = true;
    current_audio = a;
    raw_player_set_native_guard(native_guard, a);
done:
    a->loading = NULL;
    return a;
}


static void report_clock_sample(FILE *file, const char *name, const PerformanceClockSample *s)
{
    fprintf(file, "audio_clock_%s target_hz=%lu actual_hz=%lu matches=%u pll=%08lx "
            "control=%08lx emi74=%08lx emi1c=%08lx emi_control=%08lx\n", name,
            (unsigned long)s->target_hz, (unsigned long)s->actual_hz, s->matches,
            (unsigned long)s->pll, (unsigned long)s->control, (unsigned long)s->timing74,
            (unsigned long)s->timing1c, (unsigned long)s->emi_control);
}

static void save_report(AudioPlayback *a, const char *event)
{
    bool wanted = (a->failed && a->source_error!=ECANCELED) || a->diagnostics_error;
#if defined(NDVIDEO_MEDIA_TIMINGS) && NDVIDEO_MEDIA_TIMINGS
    wanted = true;
#endif
    /* Healthy release playback is quiet. Failed audio always leaves evidence;
     * the explicit local timing build also records successful movie closes. */
    if (!wanted)
        return;
    raw_player_before_native();
    char timing_path[MAX_PATH_LEN];
    if (player_data_prepare() && player_data_path("ndmedia-timing.log.tns", timing_path, sizeof(timing_path))) {
        FILE *file = player_error_log_open(timing_path,true);
        if (file) {
            fprintf(file, "media timings | %s %s restarts=%u seeks=%u pause_max_ms=%lu seek_max_ms=%lu prefill_max_ms=%lu start_max_ms=%lu read_ms=%lu decode_ms=%lu\n",
                __DATE__, __TIME__, a->restart_count, a->seek_count,
                (unsigned long)a->pause_max_ms, (unsigned long)a->seek_max_ms,
                (unsigned long)a->prefill_max_ms, (unsigned long)a->start_max_ms,
                (unsigned long)a->stats.read_ms, (unsigned long)a->stats.decode_ms);
            fprintf(file, "last_resume seek_ms=%lu prefill_ms=%lu start_ms=%lu\n",
                (unsigned long)a->last_seek_ms, (unsigned long)a->last_prefill_ms,
                (unsigned long)a->last_start_ms);
            fprintf(file, "audio failed=%u notice=%s hardware_stage=%s hardware_result=%d\n",
                a->failed, a->text, a->hardware_stage ? a->hardware_stage : "not entered", a->hardware_result);
            if (*a->transfer_fault)
                fprintf(file,"audio_primary_transfer_fault=%s\n",a->transfer_fault);
            fprintf(file,"audio_source errno=%d cursor=%llu bytes=%llu\n",a->source_error,
                (unsigned long long)a->cursor,(unsigned long long)a->bytes);
            fprintf(file,"audio_path=%s\n",a->source_path);
            fprintf(file, "media_event=%s frame=%lu frame_count=%lu codec=%s clock_samples=%u\n",
                event, (unsigned long)a->context_position, (unsigned long)a->context_total,
                a->context_codec, a->clock_samples);
            report_clock_sample(file, "before_usb", &a->clock_before_usb);
            report_clock_sample(file, "after_usb", &a->clock_after_usb);
            if(a->diagnostics)a->diagnostics(file,a->diagnostics_context);
            performance_clock_debug(file);
            usb_audio_hub_debug(file);
            if (a->decoder) audio_decoder_debug(a->decoder, file);
#if NDVIDEO_CODEC_MODULES || NDVIDEO_AUDIO_MODULES
            fprintf(file,"decoder_modules_loaded_bytes=%lu\n",(unsigned long)codec_modules_loaded_bytes());
#endif
            if (a->failed) {
                usb_host_session_debug(file);
                usb_audio_topology_debug(file);
            }
            fclose(file);
        }
    }
}


void audio_playback_context(AudioPlayback *a, uint32_t position, uint32_t total, const char *codec)
{
    if (!a) return;
    a->context_position=position; a->context_total=total; a->context_codec=codec?codec:"audio";
}
void audio_playback_checkpoint(AudioPlayback *a, const char *event)
{ if (a) save_report(a, event); }
static void close_playback(AudioPlayback *a, bool report)
{
    if (!a) return;
    close_with_retry(a);
    if (report) save_report(a, "close");
    if (current_audio == a) {
        current_audio=NULL;
        raw_player_set_native_guard(NULL,NULL);
    }
    audio_decoder_close(a->decoder);
    audio_buffer_close(&a->buffer);
    if (a->file) movie_file_close(a->file);
#if defined(NDVIDEO_MEDIA_TIMINGS) && NDVIDEO_MEDIA_TIMINGS && NDVIDEO_AUDIO_MODULES
    char path[MAX_PATH_LEN];
    if (report && player_data_prepare() && player_data_path("ndmedia-timing.log.tns",path,sizeof(path))) {
        FILE *file=fopen(path,"ab"); /* Continuation of the report above. */
        if (file) {
            fprintf(file,"audio_modules_after_close bytes=%lu refs=%u,%u,%u error=%s\n",
                    (unsigned long)codec_modules_loaded_bytes(),
                    codec_module_references(CODEC_MODULE_VORBIS),codec_module_references(CODEC_MODULE_AAC),
                    codec_module_references(CODEC_MODULE_OPUS),codec_module_error());
            fclose(file);
        }
    }
#endif
    free(a);
}
void audio_playback_close(AudioPlayback *a) { close_playback(a,true); }
void audio_playback_discard_retry(AudioPlayback *a) { close_playback(a,false); }
bool audio_playback_can_reconnect(const AudioPlayback *a)
{
    return a && a->failed && a->source_error != ECANCELED &&
        (a->usb_failed || (usb_path(a->source_path) &&
            (a->source_error == ENODEV || a->source_error == EIO)));
}
bool audio_playback_cancelled(const AudioPlayback *a)
{ return a && a->source_error == ECANCELED; }
void audio_playback_suspend(AudioPlayback *a)
{ if (a && a->usb) close_with_retry(a); }


void audio_playback_pause(AudioPlayback *a)
{
    if (!a || !a->usb || !a->running) return;
    uint32_t started = monotonic_clock_now_ms();
    raw_player_before_read();
    if (!usb_audio_pause(a->usb)) {
        capture_transfer_fault(a);
        notice(a, usb_audio_error(a->usb));
        a->failed = true;
        close_with_retry(a);
        return;
    }
    a->position = usb_audio_position(a->usb);
    a->running = false;
    record_elapsed(&a->pause_max_ms, started);
}


bool audio_playback_seek(AudioPlayback *a, uint32_t frame)
{
    if (!a || !a->decoder || a->failed || frame>a->buffer.sample_count) return false;
    audio_playback_pause(a);
    if (a->failed) return false;
    a->position=(uint64_t)frame<<32;
    a->need_seek=true;
    return true;
}
bool audio_playback_retry(AudioPlayback *a, uint32_t frame)
{
    if (!a || !a->decoder) return false;
    audio_playback_suspend(a);
    a->failed=false; a->source_error=0; a->mute=false;remembered_mute=false;
    a->transfer_fault[0]=0;
    a->usb_failed=false;
    bool ok=audio_playback_seek(a,frame);
    if (ok) notice(a,"Retrying USB audio");
    return ok;
}


void audio_playback_pump(AudioPlayback *a)
{
    if (!a || !a->running) return;
    if (!usb_audio_service(a->usb)) {
        capture_transfer_fault(a);
        a->position = usb_audio_position(a->usb);
        notice(a, usb_audio_error(a->usb));
        a->failed = true;
        a->running = false;
    }
}

static void tick_audio(AudioPlayback *a, bool paused, unsigned num, unsigned den)
{
    if (!a || !a->decoder) return;
    if (a->failed) {
        audio_playback_suspend(a);
        return;
    }
    if (paused) {
        audio_playback_pause(a);
        return;
    }
    if (a->running && (num != a->num || den != a->den))
        audio_playback_pause(a);
    if (a->failed)
        return;
    if (!a->need_seek && audio_decoder_eof(a->decoder) &&
        (audio_playback_position(a)>>32)>=a->buffer.sample_count) {
        audio_playback_pause(a);
        a->position=(uint64_t)a->buffer.sample_count<<32;
        return;
    }
    bool retained_resume = a->usb && !a->running && !a->need_seek;
    if (!a->running) a->last_seek_ms = 0;
    if (a->need_seek) {
        uint32_t started = monotonic_clock_now_ms();
        ++a->seek_count;
        if (!audio_decoder_seek(a->decoder, (uint32_t)(a->position >> 32))) {
            notice(a, "Audio seek failed");
            a->failed = true;
            return;
        }
        a->need_seek = false;
        a->last_seek_ms = monotonic_clock_now_ms() - started;
        record_elapsed(&a->seek_max_ms, started);
    }
    uint64_t position = a->running ? usb_audio_position(a->usb) : a->position;
    uint32_t target = a->buffer.sample_rate;
    /* A retained pause already has queued PCM. Restart from that reserve;
     * ordinary running ticks replenish the full second in bounded slices. */
    if (retained_resume && num <= (uint64_t)den * 2U)
        target /= 2;
    if (target > a->buffer.pcm_capacity - 1024)
        target = a->buffer.pcm_capacity - 1024;
    uint32_t started = monotonic_clock_now_ms();
    while (!audio_decoder_eof(a->decoder) &&
           a->buffer.pcm_end - (uint32_t)(position >> 32) < target) {
        if(a->loading && !progress(a)) {
            notice(a,"Audio connection cancelled");a->failed=true;break;
        }
        if (audio_decoder_step(a->decoder, position) < 0) {
            notice(a, audio_decoder_error(a->decoder));
            a->failed = true;
            break;
        }
        audio_playback_pump(a);
        if (a->running)
            position = usb_audio_position(a->usb);
        if (a->running && monotonic_clock_now_ms() - started >= 8 &&
            a->buffer.pcm_end - (uint32_t)(position >> 32) >= a->buffer.sample_rate / 2)
            break;
    }
    if (!a->running) {
        a->last_prefill_ms = monotonic_clock_now_ms() - started;
        record_elapsed(&a->prefill_max_ms, started);
    }
    if (a->failed) {
        audio_playback_suspend(a);
        return;
    }
    if (audio_decoder_eof(a->decoder) && (a->position>>32)>=a->buffer.sample_count && !a->running)
        return;
    if (!a->usb && !hardware_open(a)) {
        a->usb_failed = true;
        a->failed = true;
        return;
    }
    if (!a->running) {
        uint32_t started = monotonic_clock_now_ms();
        ++a->restart_count;
        a->num = num;
        a->den = den;
        if (!usb_audio_start(a->usb, &a->buffer, a->position, num, den, a->mute ? 0 : a->volume)) {
            capture_transfer_fault(a);
            notice(a, usb_audio_error(a->usb));
            a->failed = true;
            audio_playback_suspend(a);
            return;
        }
        a->running = true;
        a->last_start_ms = monotonic_clock_now_ms() - started;
        record_elapsed(&a->start_max_ms, started);
    }
    audio_playback_pump(a);
}

void audio_playback_tick_loading(AudioPlayback *a, LoadingProgress *loading, bool paused,
                                 unsigned num, unsigned den)
{
    if (!a) return;
    a->loading = loading;
    tick_audio(a,paused,num,den);
    a->loading = NULL;
}

void audio_playback_tick(AudioPlayback *a, bool paused, unsigned num, unsigned den)
{
    if (!a) return;
    uint32_t sample = (uint32_t)(a->position >> 32);
    bool seek_reads = a->need_seek && sample < a->buffer.sample_count &&
        (sample < a->buffer.pcm_begin || sample >= a->buffer.pcm_end);
    if ((a->usb && !seek_reads) || a->failed || paused) {
        tick_audio(a, paused, num, den);
        return;
    }
    /* Negotiation and the first decode happen at an explicit loading boundary.
     * Keep the existing progress UI responsive, including cancellation, rather
     * than leaving a motionless movie on screen during a missing-device wait. */
    SDL_Surface *snapshot = capture_screen_surface(a->screen);
    LoadingProgress loading;
    loading_progress_init(&loading, a->screen, snapshot, a->fonts,
                          a->usb ? "Seeking audio" : "Connecting audio", true);
    a->loading = &loading;
    loading_progress_tick(&loading, true);
    tick_audio(a, paused, num, den);
    a->loading = NULL;
    if (snapshot) {
        SDL_BlitSurface(snapshot, NULL, a->screen, NULL);
        present_screen(a->screen);
        SDL_FreeSurface(snapshot);
    }
}


bool audio_playback_active(const AudioPlayback *a)
{ return a && a->running && !a->failed; }
void audio_playback_idle(AudioPlayback *a)
{ if (audio_playback_active(a)) audio_playback_tick(a,false,a->num,a->den); }
bool audio_playback_source_lost(const AudioPlayback *a)
{ return a && a->source_error==ENODEV; }
uint64_t audio_playback_position(const AudioPlayback *a)
{ return a ? a->running ? usb_audio_position(a->usb) : a->position : 0; }
bool audio_playback_info(const AudioPlayback *a, AudioPlaybackInfo *info)
{
    if (!a || !info) return false;
    uint64_t position=audio_playback_position(a)>>32;
    if (position>a->buffer.sample_count) position=a->buffer.sample_count;
    *info=(AudioPlaybackInfo){a->buffer.sample_rate,a->buffer.sample_count,(uint32_t)position,
        a->buffer.channels,a->stats.codec,a->buffer.sample_count!=UINT32_MAX,
        a->running,a->failed,a->decoder && !a->need_seek && audio_decoder_eof(a->decoder) &&
            position>=a->buffer.sample_count};
    return true;
}

size_t audio_playback_snapshot(const AudioPlayback *a,int16_t *out,size_t frames)
{
    if(!out)return 0;
    if(frames>1024)frames=1024;
    memset(out,0,frames*2*sizeof(*out));
    if(!a || !a->running || a->failed || a->need_seek)return 0;
    const AudioBuffer *b=&a->buffer;
    if(!b->pcm || !b->channels)return 0;
    uint64_t position=audio_playback_position(a)>>32;
    size_t copied=0;
    for(size_t i=0;i<frames;++i){
        uint64_t index=position+i;
        if(index>=b->sample_count || (b->pcm_capacity &&
           (index<b->pcm_begin || index>=b->pcm_end)))break;
        size_t slot=b->pcm_capacity?index%b->pcm_capacity:index;
        const int16_t *p=b->pcm+slot*b->channels;
        out[2*i]=p[0];out[2*i+1]=p[b->channels==1?0:1];++copied;
    }
    return copied;
}


const char *audio_playback_status(AudioPlayback *a)
{
    if (!a || !a->notice)
        return NULL;
    a->notice = false;
    return a->text;
}

bool audio_playback_level(const AudioPlayback *a, AudioPlaybackLevel *level)
{
    if (!a || !a->decoder) return false;
    if (level) *level=(AudioPlaybackLevel){a->volume,a->mute,a->failed};
    return true;
}

void audio_playback_set_volume(AudioPlayback *a, unsigned volume)
{
    if (!a) return;
    a->volume = volume > 100 ? 100 : volume;
    a->mute = false;
    remembered_volume=a->volume;remembered_mute=false;
    if (a->usb)
        usb_audio_volume(a->usb, a->volume);
}

void audio_playback_volume(AudioPlayback *a, int change)
{
    if (!a) return;
    int value = (int)a->volume + change;
    audio_playback_set_volume(a, value < 0 ? 0 : (unsigned)value);
    snprintf(a->text, sizeof(a->text), "Volume %u%%", a->volume);
    a->notice = true;
}


void audio_playback_mute(AudioPlayback *a)
{
    if (!a) return;
    if (a->failed) { audio_playback_retry(a,(uint32_t)(a->position>>32)); return; }
    a->mute=!a->mute;
    remembered_mute=a->mute;
    if (a->usb) usb_audio_volume(a->usb,a->mute?0:a->volume);
}


void audio_playback_debug(const AudioPlayback *a, FILE *file)
{
    if (!a) return;
    fprintf(file,
            "audio running=%u failed=%u volume=%u rate=%lu source_bytes=%llu read_ms=%lu "
            "decode_ms=%lu\n",
            a->running, a->failed, a->volume, (unsigned long)a->buffer.sample_rate,
            (unsigned long long)a->bytes, (unsigned long)a->stats.read_ms,
            (unsigned long)a->stats.decode_ms);
    fprintf(file, "audio_notice=%s position=%llu transfer_fault=%s\n", a->text,
            (unsigned long long)(a->position >> 32),
            *a->transfer_fault ? a->transfer_fault : usb_audio_transfer_fault());
    audio_decoder_debug(a->decoder, file);
    usb_host_session_debug(file);
    usb_audio_topology_debug(file);
}

void audio_playback_diagnostics(AudioPlayback *a,void (*callback)(FILE *,const void *),const void *context,bool error)
{ if(a){a->diagnostics=callback;a->diagnostics_context=context;a->diagnostics_error=error;} }
