#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "player/performance_clock.h"
#include "audio/audio_playback.h"
#include "audio/movie_audio.h"
struct AudioPlayback {
    bool failed,primary_saved;
    unsigned restart_count,seek_count;
    uint32_t pause_max_ms,seek_max_ms,prefill_max_ms,start_max_ms;
    uint32_t last_seek_ms,last_prefill_ms,last_start_ms;
    struct {uint32_t read_ms,decode_ms;} stats;
    char text[16];const char *hardware_stage;int hardware_result,source_error;
    char transfer_fault[384];
    uint64_t cursor,bytes;
    PerformanceClockSample clock_before_usb,clock_after_usb;
    unsigned clock_samples;
    void *decoder;
    uint32_t context_position,context_total;
    const char *context_codec;
    char source_path[256];
    void (*diagnostics)(FILE *,const void *);const void *diagnostics_context;bool diagnostics_error;
};
typedef AudioPlayback MovieAudio;
typedef struct Movie {
    MovieAudio *audio;
    uint32_t current_frame;
    struct {uint32_t frame_count;} header;
    unsigned codec;
} Movie;
#define MAX_PATH_LEN 256
static unsigned native_calls,opens,closes;
static bool owns_usb;
static FILE *output,*primary;
static void raw_player_before_native(void) {++native_calls;owns_usb=false;}
static bool player_data_prepare(void) {assert(!owns_usb);return true;}
static bool player_data_path(const char *name,char *path,size_t bytes)
{assert(!owns_usb);snprintf(path,bytes,"%s",name);return true;}
static FILE *report_open(const char *path,const char *mode)
{(void)mode;assert(!owns_usb);++opens;return strstr(path,"audio-fault")?primary:output;}
static FILE *player_error_log_open(const char *path,bool append)
{return report_open(path,append?"ab":"wb");}
static int report_close(FILE *file) {assert(file==output || file==primary);++closes;return 0;}
static const char *movie_codec_name(unsigned codec) {(void)codec;return "h264";}
void performance_clock_debug(FILE *f) {(void)f;}
static void usb_audio_hub_debug(FILE *f) {fputs("hub evidence\n",f);}
static void audio_decoder_debug(void *d,FILE *f) {(void)d;(void)f;}
static void usb_host_session_debug(FILE *f) {(void)f;}
static void usb_audio_topology_debug(FILE *f) {(void)f;}
static const char *usb_audio_last_stream(void) {return "captured stream";}
static const char *usb_audio_last_control(void) {return "captured control";}
#define fopen report_open
#define fclose report_close
/* PRODUCTION_REPORT */
#undef fopen
#undef fclose
int main(void)
{
    MovieAudio a={.hardware_stage="ready",.clock_samples=1};
    Movie m={.audio=&a,.current_frame=25,.header={100}};
    output=tmpfile();primary=tmpfile();assert(output && primary);
    owns_usb=true;
    movie_audio_checkpoint(&m);
#if defined(NDVIDEO_MEDIA_TIMINGS) && NDVIDEO_MEDIA_TIMINGS
    assert(opens==1 && closes==1 && native_calls==1 && !owns_usb);
    rewind(output);char log[4096];size_t bytes=fread(log,1,sizeof(log)-1,output);log[bytes]=0;
    assert(strstr(log,"media_event=clock menu frame=25"));
    assert(strstr(log,"audio_clock_after_usb") && strstr(log,"hub evidence"));
#else
    assert(!opens && !closes && !native_calls && owns_usb);
#endif
    unsigned before=opens;
    a.failed=true;a.restart_count=1;owns_usb=true;
    strcpy(a.transfer_fault,"stream token=12345678; saved before cleanup");
    movie_audio_checkpoint(&m);
    assert(opens==before+2 && opens==closes && !owns_usb && a.primary_saved);
    rewind(primary);char pinned[4096];size_t pinned_bytes=fread(pinned,1,sizeof(pinned)-1,primary);pinned[pinned_bytes]=0;
    assert(strstr(pinned,"primary=stream token=12345678; saved before cleanup"));
    assert(strstr(pinned,"usb_stream_before_close=captured stream"));
    rewind(output);char report[8192];size_t count=fread(report,1,sizeof(report)-1,output);report[count]=0;
    assert(strstr(report,"audio_primary_transfer_fault=stream token=12345678; saved before cleanup"));
    movie_audio_checkpoint(NULL);m.audio=NULL;movie_audio_checkpoint(&m);
    assert(opens==before+2);
    MovieAudio retry={.failed=true,.hardware_stage="setup failed"};
    strcpy(retry.transfer_fault,"later setup failure");
    save_report(&retry,"retry");
    assert(!retry.primary_saved && opens==before+3);
    rewind(primary);char retained[4096];size_t retained_bytes=fread(retained,1,sizeof(retained)-1,primary);
    assert(retained_bytes==pinned_bytes && !memcmp(pinned,retained,pinned_bytes));
    save_report(&a,"same incident");
    assert(opens==before+4); /* No second pinned write from this stream. */
    fclose(output);
    fclose(primary);
    puts("PASS media checkpoints write only on error/explicit diagnostics and after USB ownership release");
    return 0;
}
