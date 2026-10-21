#ifndef NMEDIA_USB_PHASE_TIMING_H
#define NMEDIA_USB_PHASE_TIMING_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifndef NDVIDEO_USB_TIMINGS
#define NDVIDEO_USB_TIMINGS 0
#endif

/* Diagnostic-only elapsed time, including native scheduling and cooperative
 * I/O waits. Nested phases are inclusive and must not be added together. */
enum UsbPhaseTimingPhase {
    USB_PHASE_APP_STARTUP, USB_PHASE_RESOLVE, USB_PHASE_NATIVE_UI,
    USB_PHASE_DRIVER_REGISTER, USB_PHASE_ENUMERATION, USB_PHASE_HOST_WAKE,
    USB_PHASE_OWNERSHIP, USB_PHASE_ENDPOINTS, USB_PHASE_SCSI, USB_PHASE_MOUNT,
    USB_PHASE_DIRECTORY_OPEN, USB_PHASE_DIRECTORY_READ, USB_PHASE_FOLDER_SCAN,
    USB_PHASE_AUDIO_SETUP, USB_PHASE_DISCOVERY, USB_PHASE_IO_PREPARE, USB_PHASE_FS_WORKER,
    USB_PHASE_MOUNT_FS, USB_PHASE_MOUNT_SERIAL, USB_PHASE_MOUNT_ROOT,
    USB_PHASE_DISK_READ, USB_PHASE_FS_PROGRESS, USB_PHASE_CONNECTION, USB_PHASE_DIALOG_DRAW,
    USB_PHASE_LIBRARY_READY,
    USB_PHASE_COUNT
};
typedef struct { uint32_t started_ms, generation; } UsbPhaseTimingToken;
typedef struct {
    uint32_t calls, nonzero_results, min_ms, max_ms, first_ms, last_ms;
    uint64_t total_ms;
} UsbPhaseTimingStats;
enum {
    USB_QUEUE_VALID=1U, USB_QUEUE_MMIO=2U, USB_QUEUE_ANCHOR_LIVE=4U,
    USB_QUEUE_ANCHOR_FREE=8U, USB_QUEUE_LIVE_TRUNCATED=16U,
    USB_QUEUE_FREE_TRUNCATED=32U, USB_QUEUE_BAD_LIVE=64U, USB_QUEUE_BAD_FREE=128U,
    USB_QUEUE_FIRST_QTD_VALID=256U, USB_QUEUE_ANCHOR_QTD_VALID=512U
};
typedef struct {
    uint32_t flags,command,status,port,otg,async_list;
    uint32_t head,head_pa,head_next,free_head,live_nodes,free_nodes;
    uint32_t first_qh,first_info,first_token,first_next,first_qtd_token;
    uint32_t anchor,anchor_info,anchor_token,anchor_next,anchor_qtd_token;
    uint32_t irq,active,suspended,bus_suspended;
    uint32_t event,event_flags,event_waiters,pending,irq_mask,usb_count,tree_count,head_token;
} UsbQueueTimingState;
typedef struct {
    uint32_t phase,error,saved_list,saved_command,queue,descriptor,token,timeout_phase;
    int native_result;
} UsbQueueTimingPrivate;

#if NDVIDEO_USB_TIMINGS
/* The caller supplies its established monotonic domain. No hardware access,
 * allocation, file I/O or scheduler changes occur in the recorder. */
void usb_phase_timing_init(uint32_t (*clock_ms)(void));
void usb_phase_timing_set_enabled(bool enabled);
bool usb_phase_timing_enabled(void);
void usb_phase_timing_reset(void);
UsbPhaseTimingToken usb_phase_timing_begin(enum UsbPhaseTimingPhase phase);
void usb_phase_timing_end(enum UsbPhaseTimingPhase phase, UsbPhaseTimingToken token, int result);
/* Directory opens keep a bounded path hash and sanitized final component,
 * never the full path. Uses the same fixed span retention as other phases. */
void usb_phase_timing_directory_end(UsbPhaseTimingToken token, const char *path, int result);
/* Aggregate read request size only, without retaining LBAs or payload bytes. */
void usb_phase_timing_disk_read_end(UsbPhaseTimingToken token, uint32_t sector_bytes,
                                    uint32_t count, int result);
bool usb_phase_timing_snapshot(enum UsbPhaseTimingPhase phase, UsbPhaseTimingStats *out);
/* Keeps the first 32 and latest 32 selected spans, plus all aggregates.
 * Routine repeated waits/reads retain only first/new-maximum/error events.
 * Called only at the application's existing safe diagnostic write boundary. */
void usb_phase_timing_debug(FILE *file);
/* Static stage labels only. Retains first 8/latest 8 boundary snapshots and
 * one first actual private-transport failure independently of that ring.
 * The native producer reads authenticated CPU-visible metadata and already
 * readable MMIO; it never ungates hardware or changes DMA/cache/IRQ policy. */
void usb_phase_timing_queue_state(const char *stage,const UsbQueueTimingState *,const UsbQueueTimingPrivate *);
void native_usb_ui_queue_capture(const char *stage,const UsbQueueTimingPrivate *);
#else
static inline void usb_phase_timing_init(uint32_t (*clock_ms)(void)) { (void)clock_ms; }
static inline void usb_phase_timing_set_enabled(bool enabled) { (void)enabled; }
static inline bool usb_phase_timing_enabled(void) { return false; }
static inline void usb_phase_timing_reset(void) { }
static inline UsbPhaseTimingToken usb_phase_timing_begin(enum UsbPhaseTimingPhase phase)
{ (void)phase; return (UsbPhaseTimingToken){0, 0}; }
static inline void usb_phase_timing_end(enum UsbPhaseTimingPhase phase, UsbPhaseTimingToken token, int result)
{ (void)phase; (void)token; (void)result; }
static inline void usb_phase_timing_directory_end(UsbPhaseTimingToken token, const char *path, int result)
{ (void)token; (void)path; (void)result; }
static inline void usb_phase_timing_disk_read_end(UsbPhaseTimingToken token, uint32_t sector_bytes,
                                                uint32_t count, int result)
{ (void)token; (void)sector_bytes; (void)count; (void)result; }
static inline bool usb_phase_timing_snapshot(enum UsbPhaseTimingPhase phase, UsbPhaseTimingStats *out)
{ (void)phase; if (out) *out = (UsbPhaseTimingStats){0}; return false; }
static inline void usb_phase_timing_debug(FILE *file) { (void)file; }
static inline void usb_phase_timing_queue_state(const char *stage,const UsbQueueTimingState *state,
                                               const UsbQueueTimingPrivate *private_state)
{ (void)stage; (void)state; (void)private_state; }
static inline void native_usb_ui_queue_capture(const char *stage,const UsbQueueTimingPrivate *private_state)
{ (void)stage; (void)private_state; }
#endif
#endif
