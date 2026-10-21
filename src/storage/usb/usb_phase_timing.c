#include "usb_phase_timing.h"
#if NDVIDEO_USB_TIMINGS
#include <limits.h>
#include <string.h>

#define FIRST_SPANS 32U
#define RECENT_SPANS 32U
typedef struct {
    uint64_t sequence;
    uint32_t phase, started_ms, elapsed_ms;
    int result;
    uint32_t path_hash;
    char leaf[32];
} PhaseSpan;
static uint32_t (*phase_clock)(void);
static bool recording;
static uint32_t generation;
static uint64_t spans_seen, events_seen;
static UsbPhaseTimingStats statistics[USB_PHASE_COUNT];
static PhaseSpan spans[FIRST_SPANS + RECENT_SPANS];
static uint64_t requested_sectors, completed_sectors, completed_bytes;
typedef struct {
    uint64_t sequence;
    const char *stage;
    uint32_t at_ms;
    UsbQueueTimingState native;
    UsbQueueTimingPrivate private_state;
} QueueState;
static QueueState queues[16];
static uint64_t queues_seen;
static QueueState first_transfer_failure;
static bool first_transfer_failure_valid;
static const char *const names[USB_PHASE_COUNT] = {
    "app_startup", "resolve", "native_ui", "driver_register", "native_wait",
    "host_wake", "ownership", "endpoints", "scsi_init", "mount",
    "directory_open", "directory_read", "folder_scan", "audio_setup",
    "discovery", "io_prepare", "fs_worker", "mount_fs", "mount_serial", "mount_root",
    "disk_read", "fs_progress", "connection_total", "dialog_draw", "connection_to_first_usb_rows",
    "native_pipe_abort", "native_pipe_close", "native_owner_busy"
};
static void invalidate_tokens(void)
{ if (!++generation) generation = 1; }
void usb_phase_timing_reset(void)
{
    invalidate_tokens();
    spans_seen = events_seen = 0;
    requested_sectors = completed_sectors = completed_bytes = 0;
    queues_seen = 0;
    first_transfer_failure_valid = false;
    memset(statistics, 0, sizeof(statistics));
    memset(spans, 0, sizeof(spans));
    memset(queues, 0, sizeof(queues));
    memset(&first_transfer_failure, 0, sizeof(first_transfer_failure));
}
void usb_phase_timing_init(uint32_t (*clock_ms)(void))
{
    phase_clock = clock_ms;
    recording = clock_ms != NULL;
    usb_phase_timing_reset();
}
void usb_phase_timing_set_enabled(bool enabled)
{
    if (recording != (enabled && phase_clock != NULL)) invalidate_tokens();
    recording = enabled && phase_clock != NULL;
}
bool usb_phase_timing_enabled(void) { return recording; }
UsbPhaseTimingToken usb_phase_timing_begin(enum UsbPhaseTimingPhase phase)
{
    if (!recording || (unsigned)phase >= USB_PHASE_COUNT)
        return (UsbPhaseTimingToken){0, 0};
    return (UsbPhaseTimingToken){phase_clock(), generation};
}
static void end_span(enum UsbPhaseTimingPhase phase, UsbPhaseTimingToken token, int result,
                     const char *path)
{
    if (!recording || !token.generation || token.generation != generation ||
        (unsigned)phase >= USB_PHASE_COUNT) return;
    uint32_t now = phase_clock(), elapsed = now - token.started_ms;
    UsbPhaseTimingStats *s = statistics + phase;
    bool first = !s->calls, new_maximum = elapsed > s->max_ms;
    if (!s->calls) { s->first_ms = token.started_ms; s->min_ms = elapsed; }
    if (s->calls != UINT32_MAX) ++s->calls;
    if (result && s->nonzero_results != UINT32_MAX) ++s->nonzero_results;
    if (elapsed < s->min_ms) s->min_ms = elapsed;
    if (elapsed > s->max_ms) s->max_ms = elapsed;
    s->last_ms = now;
    s->total_ms += elapsed;
    ++spans_seen;
    bool sampled = phase == USB_PHASE_ENUMERATION || phase == USB_PHASE_DIRECTORY_READ ||
                   phase == USB_PHASE_DISCOVERY || phase == USB_PHASE_DISK_READ ||
                   phase == USB_PHASE_FS_PROGRESS || phase == USB_PHASE_RESOLVE ||
                   phase == USB_PHASE_DIALOG_DRAW;
    if (sampled && !first && !new_maximum && !result) return;
    unsigned slot = events_seen < FIRST_SPANS ? (unsigned)events_seen :
        FIRST_SPANS + (unsigned)((events_seen - FIRST_SPANS) % RECENT_SPANS);
    PhaseSpan *span = spans + slot;
    *span = (PhaseSpan){.sequence=spans_seen, .phase=(uint32_t)phase,
        .started_ms=token.started_ms, .elapsed_ms=elapsed, .result=result};
    if (path) {
        /* Caller supplies a bounded filesystem path. Bound reads as well as
         * output, and copy only the final component so no parent path leaks. */
        uint32_t hash = 2166136261U;
        unsigned bytes = 0, leaf = 0;
        for (; bytes < 1024U && path[bytes]; ++bytes) {
            unsigned char c = (unsigned char)path[bytes];
            hash = (hash ^ c) * 16777619U;
            if (c == '/' || c == '\\') leaf = bytes + 1U;
        }
        span->path_hash = hash;
        unsigned copied = 0;
        for (; leaf < bytes && copied + 1U < sizeof(span->leaf); ++leaf, ++copied) {
            unsigned char c = (unsigned char)path[leaf];
            span->leaf[copied] = c > 32 && c < 127 && c != '"' ? (char)c : '_';
        }
        if (!copied) span->leaf[copied++] = '-';
        span->leaf[copied] = 0;
    }
    ++events_seen;
}
void usb_phase_timing_end(enum UsbPhaseTimingPhase phase, UsbPhaseTimingToken token, int result)
{ end_span(phase, token, result, NULL); }
void usb_phase_timing_directory_end(UsbPhaseTimingToken token, const char *path, int result)
{ end_span(USB_PHASE_DIRECTORY_OPEN, token, result, path); }
void usb_phase_timing_disk_read_end(UsbPhaseTimingToken token, uint32_t sector_bytes,
                                    uint32_t count, int result)
{
    if (!recording || !token.generation || token.generation != generation) return;
    requested_sectors += count;
    if (!result) {
        completed_sectors += count;
        completed_bytes += (uint64_t)count * sector_bytes;
    }
    end_span(USB_PHASE_DISK_READ,token,result,NULL);
}
bool usb_phase_timing_snapshot(enum UsbPhaseTimingPhase phase, UsbPhaseTimingStats *out)
{
    if (!out) return false;
    *out = (UsbPhaseTimingStats){0};
    if ((unsigned)phase >= USB_PHASE_COUNT) return false;
    *out = statistics[phase];
    return out->calls != 0;
}
static void print_span(FILE *file, const PhaseSpan *span)
{
    fprintf(file, "usb_phase_span seq=%llu phase=%s start_ms=%lu elapsed_ms=%lu result=%d",
        (unsigned long long)span->sequence, names[span->phase],
        (unsigned long)span->started_ms, (unsigned long)span->elapsed_ms, span->result);
    if (span->leaf[0]) fprintf(file, " path_hash=%08lx leaf=%s",
        (unsigned long)span->path_hash, span->leaf);
    fputc('\n', file);
}
void usb_phase_timing_queue_state(const char *stage,const UsbQueueTimingState *state,
                                 const UsbQueueTimingPrivate *private_state)
{
    if (!recording || !stage || !state) return;
    unsigned slot=queues_seen<8?(unsigned)queues_seen:8U+(unsigned)((queues_seen-8U)%8U);
    QueueState *q=queues+slot;
    *q=(QueueState){.sequence=queues_seen+1,.stage=stage,.at_ms=phase_clock(),.native=*state};
    if(private_state)q->private_state=*private_state;
    /* SCSI no-media/command-status failures can have a healthy transport.
     * Preserve the first actual transport fault even if subsequent native
     * reset/close boundaries consume the recent ring before exit. */
    if(!first_transfer_failure_valid && private_state && private_state->error &&
       !strcmp(stage,"private_transfer_failed")) {
        first_transfer_failure=*q;first_transfer_failure_valid=true;
    }
    ++queues_seen;
}
static void print_queue(FILE *file,const QueueState *q)
{
    const UsbQueueTimingState *s=&q->native;
    const UsbQueueTimingPrivate *p=&q->private_state;
    fprintf(file,"usb_queue_state seq=%llu stage=%s at_ms=%lu cpu_view=1 flags=%02lx "
        "cmd=%08lx sts=%08lx port=%08lx otg=%08lx async=%08lx "
        "head=%08lx head_pa=%08lx head_next=%08lx free_head=%08lx live_nodes=%lu free_nodes=%lu "
        "first=%08lx first_info=%08lx first_token=%08lx first_next=%08lx first_qtd=%08lx "
        "anchor=%08lx anchor_info=%08lx anchor_token=%08lx anchor_next=%08lx anchor_qtd=%08lx "
        "irq=%08lx active=%lu suspended=%lu bus_suspended=%lu "
        "event=%08lx event_flags=%08lx waiters=%lu pending=%08lx irq_mask=%08lx "
        "usb_count=%lu tree_count=%lu head_token=%08lx\n",
        (unsigned long long)q->sequence,q->stage,(unsigned long)q->at_ms,
        (unsigned long)s->flags,(unsigned long)s->command,(unsigned long)s->status,
        (unsigned long)s->port,(unsigned long)s->otg,(unsigned long)s->async_list,
        (unsigned long)s->head,(unsigned long)s->head_pa,(unsigned long)s->head_next,
        (unsigned long)s->free_head,(unsigned long)s->live_nodes,(unsigned long)s->free_nodes,
        (unsigned long)s->first_qh,(unsigned long)s->first_info,(unsigned long)s->first_token,
        (unsigned long)s->first_next,(unsigned long)s->first_qtd_token,
        (unsigned long)s->anchor,(unsigned long)s->anchor_info,(unsigned long)s->anchor_token,
        (unsigned long)s->anchor_next,(unsigned long)s->anchor_qtd_token,
        (unsigned long)s->irq,(unsigned long)s->active,(unsigned long)s->suspended,
        (unsigned long)s->bus_suspended,(unsigned long)s->event,(unsigned long)s->event_flags,
        (unsigned long)s->event_waiters,(unsigned long)s->pending,(unsigned long)s->irq_mask,
        (unsigned long)s->usb_count,(unsigned long)s->tree_count,(unsigned long)s->head_token);
    fprintf(file,"usb_queue_private seq=%llu phase=%lu error=%lu saved_list=%08lx saved_cmd=%08lx "
        "queue=%08lx descriptor=%08lx token=%08lx timeout_phase=%lu native_result=%d\n",
        (unsigned long long)q->sequence,(unsigned long)p->phase,(unsigned long)p->error,
        (unsigned long)p->saved_list,(unsigned long)p->saved_command,(unsigned long)p->queue,
        (unsigned long)p->descriptor,(unsigned long)p->token,(unsigned long)p->timeout_phase,p->native_result);
}
void usb_phase_timing_debug(FILE *file)
{
    if (!file || (!recording && !spans_seen && !queues_seen)) return;
    uint64_t kept = events_seen < FIRST_SPANS + RECENT_SPANS ? events_seen : FIRST_SPANS + RECENT_SPANS;
    fprintf(file, "usb_phase_timing version=1 enabled=%u spans=%llu events=%llu sampled_out=%llu kept=%llu omitted=%llu inclusive_ms=1\n",
        recording, (unsigned long long)spans_seen, (unsigned long long)events_seen,
        (unsigned long long)(spans_seen - events_seen), (unsigned long long)kept,
        (unsigned long long)(events_seen - kept));
    for (unsigned p = 0; p < USB_PHASE_COUNT; ++p) {
        const UsbPhaseTimingStats *s = statistics + p;
        if (!s->calls) continue;
        fprintf(file, "usb_phase_total phase=%s calls=%lu nonzero=%lu total_ms=%llu min_ms=%lu max_ms=%lu first_ms=%lu last_ms=%lu\n",
            names[p], (unsigned long)s->calls, (unsigned long)s->nonzero_results,
            (unsigned long long)s->total_ms, (unsigned long)s->min_ms, (unsigned long)s->max_ms,
            (unsigned long)s->first_ms, (unsigned long)s->last_ms);
    }
    if (statistics[USB_PHASE_DISK_READ].calls)
        fprintf(file,"usb_disk_reads calls=%lu requested_sectors=%llu completed_sectors=%llu completed_bytes=%llu\n",
            (unsigned long)statistics[USB_PHASE_DISK_READ].calls,
            (unsigned long long)requested_sectors,(unsigned long long)completed_sectors,
            (unsigned long long)completed_bytes);
    unsigned first = events_seen < FIRST_SPANS ? (unsigned)events_seen : FIRST_SPANS;
    for (unsigned i = 0; i < first; ++i) print_span(file, spans + i);
    uint64_t tail = events_seen > FIRST_SPANS ? events_seen - FIRST_SPANS : 0;
    unsigned recent = tail < RECENT_SPANS ? (unsigned)tail : RECENT_SPANS;
    unsigned oldest = tail > RECENT_SPANS ? (unsigned)(tail % RECENT_SPANS) : 0;
    for (unsigned i = 0; i < recent; ++i)
        print_span(file, spans + FIRST_SPANS + (oldest + i) % RECENT_SPANS);
    unsigned queue_first=queues_seen<8?(unsigned)queues_seen:8U;
    uint64_t queue_tail=queues_seen>8?queues_seen-8:0;
    unsigned queue_recent=queue_tail<8?(unsigned)queue_tail:8U;
    unsigned queue_oldest=queue_tail>8?(unsigned)(queue_tail%8U):0;
    fprintf(file,"usb_queue_retention seen=%llu kept=%u omitted=%llu max_walk=16\n",
        (unsigned long long)queues_seen,queue_first+queue_recent,
        (unsigned long long)(queues_seen-queue_first-queue_recent));
    for(unsigned i=0;i<queue_first;++i)print_queue(file,queues+i);
    for(unsigned i=0;i<queue_recent;++i)print_queue(file,queues+8U+(queue_oldest+i)%8U);
    if(first_transfer_failure_valid) {
        fprintf(file,"usb_queue_first_failure seq=%llu retained=1\n",
            (unsigned long long)first_transfer_failure.sequence);
        print_queue(file,&first_transfer_failure);
    }
}
#endif
