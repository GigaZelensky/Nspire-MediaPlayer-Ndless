#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "audio/audio_playback.h"
#include "audio/movie_audio.h"
typedef struct { uint64_t position; } UsbAudio;
#include "audio/audio_buffer.h"
typedef struct SDL_Surface { int unused; } SDL_Surface;
typedef struct { int unused; } Fonts;
typedef struct { int unused; } LoadingProgress;
struct AudioPlayback {
    UsbAudio *usb;
    void *decoder;
    AudioBuffer buffer;
    LoadingProgress *loading;
    SDL_Surface *screen;
    Fonts *fonts;
    uint64_t position;
    unsigned num, den, volume;
    uint32_t pause_max_ms, seek_max_ms, prefill_max_ms, start_max_ms;
    uint32_t last_seek_ms, last_prefill_ms, last_start_ms;
    unsigned restart_count, seek_count;
    bool running, failed, need_seek, mute, usb_failed, primary_saved;
    int source_error;
    char transfer_fault[384];
    char source_path[256];
    struct {unsigned codec;} stats;
};
typedef AudioPlayback MovieAudio;
typedef struct Movie {
    MovieAudio *audio;
    uint32_t current_frame;
    struct { uint32_t fps_num, fps_den; } header;
    struct { uint32_t rate,samples; } audio_track;
    struct { bool valid; } cadence;
} Movie;
typedef struct { unsigned unused; } PlaybackRate;
static uint64_t monotonic_clock_now_ticks(void) {return 100;}
static void playback_capture_reset_timeline(Movie *m) {(void)m;}
static uint64_t movie_frame_time_scaled_ticks(Movie *m, unsigned frames, const PlaybackRate *rate)
{(void)m;(void)rate;return frames;}
static UsbAudio device;
static void history_set_audio_preference(unsigned volume,bool muted){(void)volume;(void)muted;}
static unsigned opens, closes, pauses, starts, seeks, dialogs, decoder_steps;
static bool pause_ok = true;
static const char *transfer_fault="stream token=12345678";
static const char *usb_audio_transfer_fault(void) {return transfer_fault;}
static void raw_player_before_read(void) {}
static uint64_t usb_audio_position(UsbAudio *u) { return u->position; }
static bool usb_audio_pause(UsbAudio *u) { (void)u; ++pauses; return pause_ok; }
static const char *usb_audio_error(UsbAudio *u) { (void)u; return "pause failure"; }
static void notice(MovieAudio *a, const char *text) { (void)a; (void)text; }
static void usb_audio_volume(UsbAudio *u,unsigned v) {(void)u;(void)v;}
static void close_with_retry(MovieAudio *a) { ++closes; a->usb = NULL; a->running = false;transfer_fault="cleanup control timeout"; }
static bool hardware_open(MovieAudio *a) { ++opens; a->usb = &device; return true; }
static bool audio_decoder_seek(void *d, uint32_t p) { (void)d; (void)p; ++seeks; return true; }
static bool audio_decoder_eof(void *d) { (void)d; return false; }
static int audio_decoder_step(void *d, uint64_t p)
{
    (void)p;
    ++decoder_steps;
    MovieAudio *a=d;
    a->buffer.pcm_end+=1024;
    return 1;
}
static const char *audio_decoder_error(void *d) { (void)d; return "decoder failure"; }
static uint32_t monotonic_clock_now_ms(void) { return 0; }
static bool progress(void *a) { (void)a; return true; }
static bool usb_path(const char *path){return !strncmp(path,"usb:",4);}
static bool storage_idle=true,health_ok=true;
static unsigned health_calls;
static bool usb_storage_quiescent(void) {return storage_idle;}
static bool usb_audio_service(UsbAudio *u) {(void)u;return true;}
static bool usb_audio_check_health(UsbAudio *u) {(void)u;++health_calls;return health_ok;}
static void context(const Movie *m) {(void)m;}
static bool usb_audio_start(UsbAudio *u, AudioBuffer *b, uint64_t p, unsigned n,
                            unsigned d, unsigned v)
{ (void)b; (void)n; (void)d; (void)v; ++starts; u->position = p; return true; }
static SDL_Surface *capture_screen_surface(SDL_Surface *s) { (void)s; return NULL; }
static void loading_progress_init(LoadingProgress *l, SDL_Surface *s, SDL_Surface *b,
                                  Fonts *f, const char *t, bool c)
{ (void)l; (void)s; (void)b; (void)f; (void)t; (void)c; ++dialogs; }
static void loading_progress_tick(LoadingProgress *l, bool force) { (void)l; (void)force; }
static void SDL_BlitSurface(SDL_Surface *a, void *b, SDL_Surface *c, void *d)
{ (void)a; (void)b; (void)c; (void)d; }
static void present_screen(SDL_Surface *s) { (void)s; }
static void SDL_FreeSurface(SDL_Surface *s) { (void)s; }
/* PRODUCTION_PLAYBACK */
/* PRODUCTION_CLOCK */
int main(void)
{
    MovieAudio a = {.usb=&device, .decoder=&a, .running=true, .num=1, .den=1,
        .buffer={.sample_rate=1000, .sample_count=10000, .pcm_capacity=4096, .pcm_end=5000}};
    Movie m = {.audio=&a, .header={24,1},.audio_track={1000,10000}};
    device.position = (uint64_t)1234 << 32;
    uint64_t anchor, due; uint32_t frame; PlaybackRate rate={0};
    reset_playback_clock(&m,&rate,&anchor,&frame,&due);
    movie_audio_tick(&m, true, 1, 1);
    assert(!a.running && a.usb == &device && a.position == device.position);
    assert(pauses==1 && closes==0 && opens==0 && dialogs==0);
    movie_audio_tick(&m, true, 1, 1);
    assert(pauses==1);
    reset_playback_clock(&m,&rate,&anchor,&frame,&due);
    movie_audio_tick(&m, false, 1, 1);
    assert(a.running && starts==1 && closes==0 && opens==0 && dialogs==0);
    assert(seeks==0 && decoder_steps==0 && a.position==((uint64_t)1234<<32) && anchor==100 && due==101);
    /* The decoder is not at EOF. Ordinary pause/resume must still use its
     * retained PCM, with neither input decoding nor a hardware reopen/modal. */
    for(unsigned i=0;i<3;++i) {
        movie_audio_tick(&m,true,1,1);
        uint32_t begin=a.buffer.pcm_begin,end=a.buffer.pcm_end;
        movie_audio_tick(&m,false,1,1);
        assert(a.running && a.usb==&device && a.position==device.position);
        assert(a.buffer.pcm_begin==begin && a.buffer.pcm_end==end);
        assert(seeks==0 && decoder_steps==0 && opens==0 && closes==0 && dialogs==0);
    }
    assert(starts==4 && pauses==4);
    m.current_frame=48;
    reset_playback_timeline(&m,&rate,&anchor,&frame,&due);
    assert(!a.running && a.need_seek && a.position==((uint64_t)2000<<32));
    movie_audio_tick(&m, false, 1, 1);
    assert(a.running && seeks==1 && starts==5 && closes==0 && opens==0 && dialogs==0);
    movie_audio_tick(&m, false, 2, 1);
    assert(a.running && starts==6 && a.num==2 && closes==0 && opens==0);
    movie_audio_suspend(&m);
    assert(!a.usb && !a.running && closes==1);
    movie_audio_tick(&m, false, 1, 1);
    assert(a.running && opens==1 && dialogs==1);
    pause_ok=false;
    transfer_fault="stream token=12345678";
    movie_audio_tick(&m, true, 1, 1);
    assert(a.failed && !a.running && !a.usb && closes==2);
    assert(strstr(a.transfer_fault,"stream token=12345678") &&
           !strstr(a.transfer_fault,"cleanup") && !strcmp(transfer_fault,"cleanup control timeout"));
    assert(audio_playback_can_reconnect(&a));
    a.source_error=ECANCELED;assert(!audio_playback_can_reconnect(&a));
    a.source_error=EIO;a.usb_failed=false;assert(!audio_playback_can_reconnect(&a));
    strcpy(a.source_path,"usb:/disk/song.flac");assert(audio_playback_can_reconnect(&a));
    a.source_error=0;assert(!audio_playback_can_reconnect(&a));
    /* A retry must clear the decoder's latched read error through a real seek,
     * and join the video's current position rather than resume stale audio. */
    a.source_error=EIO;m.current_frame=96;pause_ok=true;
    movie_audio_mute(&m);
    assert(!*a.transfer_fault);
    assert(!a.failed && !a.source_error && a.need_seek && a.position==((uint64_t)4000<<32));
    movie_audio_tick(&m,false,1,1);
    assert(a.running && seeks==2 && opens==2);
    a.source_error=ENODEV;assert(movie_audio_source_lost(&m));
    a.source_error=EIO;assert(!movie_audio_source_lost(&m));
    assert(!movie_audio_source_lost(NULL));
    // A visualization tap follows the output clock through the ring wrap.
    // It does not decode, read storage, consume samples, or latch underruns.
    int16_t ring[16];for(unsigned i=0;i<16;++i)ring[i]=(int16_t)(100+i);
    a.running=true;a.failed=false;a.need_seek=false;a.usb=&device;
    a.buffer=(AudioBuffer){.pcm=ring,.channels=2,.sample_count=200,.sample_rate=1000,
        .pcm_capacity=8,.pcm_begin=100,.pcm_end=108};
    device.position=(uint64_t)103<<32;int16_t samples[16];
    unsigned steps_before=decoder_steps;assert(audio_playback_snapshot(&a,samples,8)==5);
    for(unsigned i=0;i<5;++i)for(unsigned ch=0;ch<2;++ch)
        assert(samples[2*i+ch]==ring[((103+i)%8)*2+ch]);
    for(unsigned i=10;i<16;++i)assert(!samples[i]);
    assert(!a.buffer.failed && !a.buffer.pcm_missed && decoder_steps==steps_before);
    assert(a.buffer.pcm_begin==100 && a.buffer.pcm_end==108);
    device.position=(uint64_t)99<<32;
    assert(!audio_playback_snapshot(&a,samples,8) && !a.buffer.failed);
    a.running=false;assert(!audio_playback_snapshot(&a,samples,8));
    for(unsigned i=0;i<16;++i)assert(!samples[i]);
    a.running=true;a.failed=false;a.usb=&device;a.transfer_fault[0]=0;
    unsigned probes_before=health_calls;
    storage_idle=false;audio_playback_pump(&a);
    assert(a.running && health_calls==probes_before);
    storage_idle=true;periodic_pump(&a);
    assert(a.running && health_calls==probes_before);
    audio_playback_pump(&a);
    assert(a.running && health_calls==probes_before+1);
    health_ok=false;transfer_fault="live device probe timeout";
    audio_playback_pump(&a);
    assert(a.failed && !a.running && a.usb_failed && a.position==device.position);
    assert(strstr(a.transfer_fault,"live device probe timeout"));
    puts("PASS live health queries run only at a quiescent foreground boundary; loss preserves position/fault and triggers recovery");
    puts("PASS playback retains headset for pause/seek/rate changes; native suspend closes; pause failure cleans up");
    return 0;
}
