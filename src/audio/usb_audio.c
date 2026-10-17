/* FOTG210/EHCI register and descriptor definitions cross-checked against
 * Linux drivers/usb/fotg210/fotg210-hcd.[ch] and the supplied usb-storage
 * experiment. This implementation owns a temporary schedule, never the OS
 * device tree. Software tests cannot establish on-calculator compatibility.
 */
#include "usb_audio.h"
#ifdef NDVIDEO_USB_AUDIO_TEST
#include "usb_audio_test_platform.h"
#else
#include <os.h>
#include <libndls.h>
#endif
#include <stdlib.h>
#include <string.h>
#ifndef NDVIDEO_USB_AUDIO_TEST
extern bool audio_dma_valid(void *, size_t);
extern bool audio_platform_cx2(void);
extern uint32_t audio_native_usb_irq_mask(void);
#endif

#ifndef NDVIDEO_USB_AUDIO_TEST
#define REG(off) (*(volatile uint32_t *)(0xB0000000U + (off)))
#define IRQ_ENABLE (*(volatile uint32_t *)0xDC000010)
#define IRQ_DISABLE (*(volatile uint32_t *)0xDC000014)
#endif
#define CMD REG(0x10)
#define STS REG(0x14)
#define INTR REG(0x18)
#define FRINDEX REG(0x1c)
#define PERIODIC REG(0x24)
#define ASYNC REG(0x28)
#define PORT REG(0x30)
#define OTG REG(0x80)
#define RUN 1U
#define PSE (1U << 4)
#define ASE (1U << 5)
#define PSS (1U << 14)
#define ASS (1U << 15)
#define HALTED (1U << 12)
#define ACTIVE (1U << 31)
#define RING 1024U
#define LEAD 192U
#define START_DELAY 8U
#define PACKET_STRIDE 1024U
#define FEEDBACK_STRIDE 32U
#define CONTROL_UNDO_MAX 24U
static char last_probe[180] = "No USB audio device probed";
static char last_transfer_fault[256] = "none";
static char last_control_state[768] = "none";
static char last_stream_state[512] = "not captured";
static unsigned tt_clears, tt_clear_errors;
/* Bounded setup evidence only: no per-packet/per-frame logging. Retained after
 * the owner is freed so a failed open can explain its discovery decision. */
typedef struct {
    UsbAudioRoute route;
    const char *stage;
    uint16_t vendor, product, config_bytes;
    unsigned formats, attempts;
    bool configuration_cached, alternate_cached;
    uint8_t selected_configuration;
    uint8_t descriptor[18], config[256];
    unsigned captured_config;
    char fault[256];
} AudioProbeTrace;
static AudioProbeTrace audio_probes[16];
static unsigned audio_probe_count;
static uint32_t audio_probe_routes[4];
static AudioProbeTrace *active_probe;
const char *usb_audio_transfer_fault(void)
{
    return last_transfer_fault;
}
static bool (*probe_progress)(void *);
static void *probe_context;
static void (*stage_callback)(const char *);
void usb_audio_set_stage_callback(void (*callback)(const char *))
{
    stage_callback = callback;
}
static void stage_event(const char *stage)
{
    if (stage_callback)
        stage_callback(stage);
}
static uint32_t preferred_rate;
static bool (*route_lookup)(void *, uint8_t, UsbAudioRoute *);
static void *route_context;
static bool (*state_lookup)(void *, uint8_t, int, UsbDeviceState *);
static void *state_context;
void usb_audio_set_state_lookup(bool (*lookup)(void *, uint8_t, int, UsbDeviceState *), void *context)
{
    state_lookup = lookup;
    state_context = context;
}
void usb_audio_set_route_lookup(bool (*lookup)(void *, uint8_t, UsbAudioRoute *), void *context)
{
    route_lookup = lookup;
    route_context = context;
}
void usb_audio_prefer_rate(uint32_t rate)
{
    preferred_rate = rate;
}
static unsigned rate_index(unsigned index)
{
    for (unsigned preferred = 0; preferred < USB_AUDIO_RATE_COUNT; ++preferred) {
        if (usb_audio_rates[preferred] != preferred_rate)
            continue;
        if (index == 0)
            return preferred;
        return index <= preferred ? index - 1 : index;
    }
    return index;
}
void usb_audio_set_progress(bool (*callback)(void *), void *context)
{
    probe_progress = callback;
    probe_context = context;
}

typedef struct {
    uint32_t next, alt, token, buffer[5];
} Qtd;
typedef struct {
    uint32_t next, info1, info2, current;
    Qtd overlay;
    uint32_t pad[4];
} Qh;
typedef struct {
    uint32_t next, transaction[8], buffer[7];
} Itd;
typedef struct {
    uint32_t next, endpoint, schedule, state, buffer[2], back, pad[9];
} Sitd;
typedef char check_qtd_size[sizeof(Qtd) == 32 ? 1 : -1];
typedef char check_qh_size[sizeof(Qh) == 64 ? 1 : -1];
typedef char check_itd_size[sizeof(Itd) == 64 ? 1 : -1];
typedef char check_sitd_size[sizeof(Sitd) == 64 ? 1 : -1];

typedef struct {
    uint8_t type, request, size;
    uint16_t value, index;
    uint8_t data[4];
} ControlUndo;
typedef struct {
    uint8_t address, port, max_control, tt_hub, tt_port;
    bool high_speed, low_speed, tt_multi;
} HubPortUndo;

struct UsbAudio {
    uint32_t (*now_ms)(void);
    uint32_t saved_command, saved_status, saved_intr, saved_periodic, saved_async, saved_irq;
    uint32_t irq_mask;
    uint32_t last_frame, frame, next, start, checked, last_ms, list_bias;
    uint64_t origin, step, generated_position, packet_phase;
    uint64_t frame_positions[RING];
    uint32_t rate_q16, prime_frames, last_feedback_frame;
    uint16_t vendor, product, max_power_ma;
    unsigned volume,gain_q15;
    bool owned, running, configured, alternate_set, unsafe_dma, high_speed;
    bool closing;
    bool last_control_stalled, last_control_short, clearing_tt, health_check, link_lost;
    uint32_t health_at, health_checks, health_errors, health_max_ms;
    unsigned health_failures;
    bool root_high_speed, low_speed, tt_multi;
    uint8_t tt_hub, tt_port, parent, port_number;
    uint8_t address, max_control, previous_configuration, previous_alternate;
    UsbAudioFormat format;
    UsbAudioFormat alternatives[USB_AUDIO_MAX_FORMATS];
    uint8_t config[4096];
    size_t config_length;
    ControlUndo undo[CONTROL_UNDO_MAX];
    unsigned undo_count;
    HubPortUndo hub_undo[16U * 31U];
    unsigned hub_undo_count;
    AudioBuffer *stream;
    void *allocation;
    uint32_t *list;
    Itd *itd, *feedback_itd;
    Sitd *sitd, *feedback_sitd;
    uint8_t *packets, *feedback, *control;
    Qh *qh;
    Qtd *qtd;
    const char *error;
    void *media;
    void (*media_cleanup)(void *);
};
static bool probe_tick(UsbAudio *u)
{
    if (probe_progress && !probe_progress(probe_context) && !u->closing) {
        u->error = "Cancelled";
        return false;
    }
    return true;
}

/* ARM926 has 32-byte data cache lines. Hardware-written status must be
 * invalidated WITHOUT cleaning stale CPU lines over DMA results. */
static void dma_clean(void *p, size_t size)
{
#ifdef NDVIDEO_USB_AUDIO_TEST
    (void)p;
    (void)size;
    usb_test_poll();
#else
    uintptr_t end = ((uintptr_t)p + size + 31) & ~(uintptr_t)31;
    for (uintptr_t a = (uintptr_t)p & ~(uintptr_t)31; a < end; a += 32)
        __asm__ volatile("mcr p15, 0, %0, c7, c14, 1" ::"r"(a) : "memory");
    unsigned zero = 0;
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" ::"r"(zero) : "memory");
#endif
}
static void dma_invalidate(void *p, size_t size)
{
#ifdef NDVIDEO_USB_AUDIO_TEST
    (void)p;
    (void)size;
    usb_test_poll();
#else
    uintptr_t end = ((uintptr_t)p + size + 31) & ~(uintptr_t)31;
    for (uintptr_t a = (uintptr_t)p & ~(uintptr_t)31; a < end; a += 32)
        __asm__ volatile("mcr p15, 0, %0, c7, c6, 1" ::"r"(a) : "memory");
#endif
}

static void *allocate_dma(size_t size)
{
#ifdef NDVIDEO_USB_AUDIO_TEST
    return usb_test_allocate(size);
#else
    return malloc(size);
#endif
}
static void free_dma(void *p)
{
#ifdef NDVIDEO_USB_AUDIO_TEST
    usb_test_free(p);
#else
    free(p);
#endif
}
static bool wait_status(UsbAudio *u, uint32_t mask, uint32_t value)
{
    uint32_t start = u->now_ms();
    for (unsigned guard = 0; guard < 2000000; ++guard) {
        probe_tick(u);
        if ((STS & mask) == value)
            return true;
        if ((uint32_t)(u->now_ms() - start) > 100)
            break;
    }
    u->error = "USB controller timeout";
    return false;
}
static void io_barrier(void)
{
#ifdef NDVIDEO_USB_AUDIO_TEST
    usb_test_poll();
#else
    unsigned zero=0;
    __asm__ volatile("mcr p15,0,%0,c7,c10,4" ::"r"(zero):"memory");
#endif
}
static bool halt_schedule(UsbAudio *u)
{
    /* ASE may still be in transition if its start was never acknowledged.
     * Halt first, rather than toggle it again or free reachable descriptors. */
    CMD &= ~RUN;
    io_barrier();
    if(!wait_status(u,HALTED,HALTED)) {u->unsafe_dma=true;return false;}
    CMD &= ~(ASE|PSE);
    io_barrier();
    if(!wait_status(u,ASS|PSS,0)) {u->unsafe_dma=true;return false;}
    return true;
}
static bool stop_schedules(UsbAudio *u)
{
    if(u->unsafe_dma) {
        if(!halt_schedule(u))return false;
        u->unsafe_dma=false;
        return true;
    }
    CMD &= ~(ASE | PSE);
    if (wait_status(u, ASS | PSS, 0))
        return true;
    /* Do not release descriptors still reachable by a live controller. */
    CMD &= ~RUN;
    if (wait_status(u, HALTED, HALTED))
        return true;
    u->unsafe_dma = true;
    return false;
}
static bool connected(void)
{
    return (PORT & 5U) == 5U && !(PORT & (1U << 7));
}
static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}
static void put16(uint8_t *p, unsigned n)
{
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}

static void qtd_init(Qtd *q, void *buffer, unsigned length, unsigned pid, unsigned toggle)
{
    memset(q, 0, sizeof(*q));
    q->next = q->alt = 1;
    q->token = (length << 16) | (toggle << 31) | (3U << 10) | (pid << 8) | 0x80;
    uintptr_t a = (uintptr_t)buffer;
    q->buffer[0] = (uint32_t)a;
    for (unsigned i = 1; i < 5; ++i)
        q->buffer[i] = (uint32_t)((a + 4096 * i) & ~(uintptr_t)4095);
}

/* Setup/teardown controls run with the periodic schedule stopped. The one
 * live exception is a read-only device-descriptor health query, issued by the
 * foreground owner only after storage has released its asynchronous queue.
 * A short IN goes directly to the status stage, never to uninitialized data. */
static bool control(UsbAudio *u, unsigned type, unsigned request, unsigned value, unsigned index,
                    void *data, unsigned length, unsigned timeout)
{
    u->last_control_stalled = false;
    u->last_control_short = false;
    bool live = u->running;
    bool health = u->health_check && type == 0x80 && request == 6 &&
                  value == 0x100 && index == 0 && length == 18;
    if (!connected() || length > 4096 || (live && !health) || u->unsafe_dma)
        return false;
    /* EHCI 4.8: publish the list only after both enable and status are zero. */
    if((CMD&ASE) || (STS&ASS)) {
        u->error="USB control schedule is still active";
        return false;
    }
    uint32_t initial_command=CMD,initial_status=STS,initial_frame=FRINDEX;
    uint8_t *setup = u->control, *payload = u->control + 4096;
    bool input = (type & 0x80) != 0;
    setup[0] = (uint8_t)type;
    setup[1] = (uint8_t)request;
    put16(setup + 2, value);
    put16(setup + 4, index);
    put16(setup + 6, length);
    memset(payload, 0, 4096);
    if (!input && length)
        memcpy(payload, data, length);
    memset(u->qh, 0, sizeof(*u->qh));
    qtd_init(u->qtd, setup, 8, 2, 0);
    qtd_init(u->qtd + 1, payload, length, input ? 1 : 0, 1);
    qtd_init(u->qtd + 2, payload, 0, input ? 0 : 1, 1);
    u->qtd[0].next = (uint32_t)(uintptr_t)(u->qtd + (length ? 1 : 2));
    u->qtd[1].next = u->qtd[1].alt = (uint32_t)(uintptr_t)(u->qtd + 2);
    u->qh->next = (uint32_t)(uintptr_t)u->qh | 2;
    u->qh->info1 = u->address | ((uint32_t)u->max_control << 16) | (1U << 14) | (1U << 15) |
                   (u->high_speed ? 2U << 12 : (u->low_speed ? 1U << 12 : 0U) | (1U << 27)) |
                   (4U << 28);
    /* Match the authenticated native FOTG control-QH layout for external TT
     * routes: completion split in microframe 3, plus hub and port routing. */
    u->qh->info2 = (1U << 30) | (u->tt_hub ? 1U << 11 : 0U) | ((uint32_t)u->tt_hub << 16) |
                   ((uint32_t)u->tt_port << 23);
    u->qh->overlay.next = (uint32_t)(uintptr_t)u->qtd;
    u->qh->overlay.alt = 1;
    dma_clean(u->control, 8192);
    dma_clean(u->qtd, 3 * sizeof(Qtd));
    dma_clean(u->qh, sizeof(Qh));
    ASYNC = (uint32_t)(uintptr_t)u->qh;
    io_barrier();
    CMD = (CMD & ~(ASE | (live ? 0U : PSE) | (1U << 6))) | RUN | ASE;
    io_barrier();
    uint32_t start = u->now_ms();
    bool started=wait_status(u,ASS,ASS);
    uint32_t started_status=STS,started_frame=FRINDEX;
    bool ok = false;
    for (unsigned guard = 0; started && guard < 2000000; ++guard) {
        if (!probe_tick(u))
            break;
        dma_invalidate(u->qtd, 3 * sizeof(Qtd));
        unsigned token = u->qtd[2].token;
        if (!(token & 0x80)) {
            ok = !(token & 0x7c);
            break;
        }
        if ((u->qtd[0].token | u->qtd[1].token) & 0x7c)
            break;
        if ((uint32_t)(u->now_ms() - start) >= timeout || !connected() || (STS & (1U << 4)))
            break;
    }
    bool short_read = false;
    if(ok && length) {
        dma_invalidate(u->qtd,3*sizeof(Qtd));
        unsigned remaining = (u->qtd[1].token >> 16) & 0x7fffU;
        /* Keep short reads rejected by this exact-length API. Separately
         * identify a clean, completed IN/status exchange so discovery can
         * use authenticated native state for GET_CONFIGURATION, just as it
         * already does for a STALL. Never confuse a timeout/error with this. */
        short_read = input && remaining && remaining <= length &&
            !((u->qtd[0].token | u->qtd[1].token | u->qtd[2].token) & 0xfcU) &&
            !(u->qtd[0].token & 0x7fff0000U) && !(u->qtd[2].token & 0x7fff0000U);
        ok=!(u->qtd[1].token&(0xfcU|0x7fff0000U));
    }
    if(!ok && !u->clearing_tt) {
        dma_invalidate(u->qh,sizeof(*u->qh));
        dma_invalidate(u->qtd,3*sizeof(Qtd));
        snprintf(last_control_state,sizeof(last_control_state),
            "phase=%s initial_cmd=%08lx initial_sts=%08lx initial_fr=%08lx "
            "start_sts=%08lx start_fr=%08lx live_cmd=%08lx live_sts=%08lx live_fr=%08lx "
            "list=%08lx expected=%08lx current=%08lx next=%08lx alt=%08lx overlay=%08lx "
            "q0=%08lx q1=%08lx q2=%08lx",
            started?"transfer":"start not acknowledged",(unsigned long)initial_command,
            (unsigned long)initial_status,(unsigned long)initial_frame,(unsigned long)started_status,
            (unsigned long)started_frame,(unsigned long)CMD,(unsigned long)STS,(unsigned long)FRINDEX,
            (unsigned long)ASYNC,(unsigned long)(uintptr_t)u->qh,(unsigned long)u->qh->current,
            (unsigned long)u->qh->overlay.next,(unsigned long)u->qh->overlay.alt,
            (unsigned long)u->qh->overlay.token,(unsigned long)u->qtd[0].token,
            (unsigned long)u->qtd[1].token,(unsigned long)u->qtd[2].token);
    }
    if(!started) {
        if(!halt_schedule(u))return false;
        u->error="USB control schedule did not start";
    } else {
        CMD &= ~ASE;
        io_barrier();
        if (!wait_status(u, ASS, 0)) {
            stop_schedules(u);
            return false;
        }
    }
    if (ok && length) {
        if (input) {
            dma_invalidate(payload, length);
            memcpy(data, payload, length);
        }
    }
    bool stalled = false;
    if (!ok) {
        dma_invalidate(u->qtd, 3 * sizeof(Qtd));
        for (unsigned i = 0; i < 3; ++i) {
            uint32_t token = u->qtd[i].token;
            /* Halt with retry budget remaining and no transaction/buffer/
             * babble/microframe error is a STALL, not a timeout to ignore. */
            if ((token & 0xfcU) == 0x40U && (token & (3U << 10))) stalled = true;
        }
        dma_invalidate(u->qh, sizeof(Qh));
        if(!u->clearing_tt) {
          snprintf(last_transfer_fault, sizeof(last_transfer_fault),
                 "control addr=%u type=%02x req=%02x value=%04x index=%04x len=%u "
                 "ep0=%u tt=%u/%u q0=%08lx q1=%08lx q2=%08lx overlay=%08lx sts=%08lx ms=%lu",
                 u->address, type, request, value, index, length, u->max_control,
                 u->tt_hub, u->tt_port, (unsigned long)u->qtd[0].token,
                 (unsigned long)u->qtd[1].token, (unsigned long)u->qtd[2].token,
                 (unsigned long)u->qh->overlay.token, (unsigned long)STS,
                 (unsigned long)(u->now_ms() - start));
          if (active_probe)
              snprintf(active_probe->fault, sizeof(active_probe->fault), "%s", last_transfer_fault);
        }
    }
    if (!ok && started && !live && u->tt_hub && !u->unsafe_dma && connected()) {
        /* A cancelled/failed split can leave the hub's TT buffer occupied.
         * Clear both endpoint-zero directions after proving our QH stopped.
         * Target the HS translator itself, so these requests cannot recurse. */
        uint8_t address = u->address, max_control = u->max_control, hub = u->tt_hub,
                port = u->tt_port;
        bool high_speed = u->high_speed, low_speed = u->low_speed, closing = u->closing;
        unsigned selector = u->tt_multi ? port : 1;
        u->address = hub;
        u->max_control = 64;
        u->high_speed = true;
        u->low_speed = false;
        u->tt_hub = u->tt_port = 0;
        u->closing = true;
        u->clearing_tt = true;
        ++tt_clears;
        bool first = control(u, 0x23, 8, (address << 4) | 0x8000, selector, NULL, 0, 100);
        bool second = control(u, 0x23, 8, address << 4, selector, NULL, 0, 100);
        if (!first || !second)
            ++tt_clear_errors;
        u->address = address;
        u->max_control = max_control;
        u->high_speed = high_speed;
        u->low_speed = low_speed;
        u->tt_hub = hub;
        u->tt_port = port;
        u->closing = closing;
        u->clearing_tt = false;
    }
    u->last_control_stalled = stalled;
    u->last_control_short = short_read;
    return ok;
}

#include "usb_audio_topology.inc"
#include "usb_audio_controls.inc"
#include "usb_audio_health.inc"

static UsbAudio *usb_session_open(uint32_t (*now_ms)(void), const char **status)
{
    strcpy(last_transfer_fault, "none");
    strcpy(last_control_state,"none");
    strcpy(last_stream_state,"not captured");
    topology_count = 0;
    topology_restore_errors = 0;
    topology_restore_skipped = 0;
    topology_handoff_reason = "none";
    hub_wake_count = hub_wake_dropped = 0;
    tt_clears = tt_clear_errors = 0;
    memset(audio_probes, 0, sizeof(audio_probes));
    memset(audio_probe_routes, 0, sizeof(audio_probe_routes));
    audio_probe_count = 0;
    active_probe = NULL;
    snprintf(last_probe, sizeof(last_probe), "No USB audio device probed");
    *status = "USB audio needs a CX II";
#ifdef NDVIDEO_USB_AUDIO_TEST
    if (!is_cx2)
        return NULL;
#else
    if (!audio_platform_cx2())
        return NULL;
#endif
    *status = "Connect USB audio via OTG before launching";
    if (!connected() || !(CMD & RUN))
        return NULL;
    *status = "USB audio requires a full/high-speed device";
    unsigned speed = (OTG >> 22) & 3;
    topology_root_speed = speed;
    if (speed != 0 && speed != 2)
        return NULL;
#ifdef NDVIDEO_USB_AUDIO_TEST
    uint32_t irq_mask = IRQ_USB;
#else
    uint32_t irq_mask = audio_native_usb_irq_mask();
#endif
    if (!irq_mask || (irq_mask & (irq_mask - 1))) {
        *status = "Unsupported native USB interrupt mapping";
        return NULL;
    }
    /* The known CX II FOTG210 has operational registers at +0x10. */
    *status = "Unrecognized USB controller";
    if ((REG(0) & 0xff) != 0x10)
        return NULL;
    UsbAudio *u = calloc(1, sizeof(*u));
    if (!u) {
        *status = "USB audio allocation failed";
        return NULL;
    }
    u->now_ms = now_ms;
    u->error = "No usable UAC1/UAC2 USB audio interface";
    u->high_speed = speed == 2;
    u->root_high_speed = u->high_speed;
    u->irq_mask = irq_mask;
    size_t size = 4096 + RING * (sizeof(Itd) + sizeof(Sitd)) * 2 +
                  RING * (PACKET_STRIDE + FEEDBACK_STRIDE) + 8192 + 4096;
    u->allocation = allocate_dma(size + 4095);
    if (!u->allocation) {
        free(u);
        *status = "USB audio allocation failed";
        return NULL;
    }
    uintptr_t a = ((uintptr_t)u->allocation + 4095) & ~(uintptr_t)4095;
    /* Ndless SDRAM allocations are identity mapped on CX II. Reject other
     * mappings instead of programming a guessed DMA address. */
    if (a < 0x10000000U || a + size > 0x14000000U
#ifndef NDVIDEO_USB_AUDIO_TEST
        || !audio_dma_valid((void *)a, size)
#endif
    ) {
        *status = "Unsupported USB DMA mapping";
        free_dma(u->allocation);
        free(u);
        return NULL;
    }
    u->list = (uint32_t *)a;
    u->itd = (Itd *)(a + 4096);
    u->feedback_itd = u->itd + RING;
    u->sitd = (Sitd *)(u->feedback_itd + RING);
    u->feedback_sitd = u->sitd + RING;
    u->packets = (uint8_t *)(u->feedback_sitd + RING);
    u->feedback = u->packets + RING * PACKET_STRIDE;
    u->control = u->feedback + RING * FEEDBACK_STRIDE;
    u->qh = (Qh *)(u->control + 8192);
    u->qtd = (Qtd *)(u->qh + 1);
    unsigned mask = TCT_Local_Control_Interrupts(-1);
    /* Allocation and native setup can yield after the host-session check.
     * Recheck at the actual takeover boundary; never save/replay a native
     * reset or async-advance request as though it were a steady command. */
    if (!connected() || !(CMD & RUN) || (CMD & (2U | (1U << 6)))) {
        TCT_Local_Control_Interrupts(mask);
        *status = "Native USB connection is still settling";
        free_dma(u->allocation);
        free(u);
        return NULL;
    }
    u->saved_command = CMD;
    u->saved_status = STS;
    u->saved_intr = INTR;
    u->saved_periodic = PERIODIC;
    u->saved_async = ASYNC;
    u->saved_irq = IRQ_ENABLE;
    IRQ_DISABLE = u->irq_mask;
    INTR = 0;
    u->owned = true;
    TCT_Local_Control_Interrupts(mask);
    if (!stop_schedules(u)) {
        *status = u->error;
        return usb_audio_close(u) ? NULL : u;
    }
    return u;
}
UsbAudio *usb_audio_open(uint32_t (*now_ms)(void), const char **status)
{
    UsbAudio *u = usb_session_open(now_ms, status);
    if (!u || u->unsafe_dma)
        return u;
    if (!usb_audio_configure(u, status))
        return usb_audio_close(u) ? NULL : u;
    return u;
}
bool usb_audio_configure(UsbAudio *u, const char **status)
{
    if (!u || u->running || u->unsafe_dma)
        return false;
    if (!find_device(u)) {
        active_probe = NULL;
        *status = u->error;
        usb_audio_describe(u, last_probe, sizeof(last_probe));
        return false;
    }
    active_probe = NULL;
    CMD = (CMD & ~(ASE | PSE | (3U << 2) | (1U << 6))) | RUN;
    for (unsigned i = 0; i < RING; ++i)
        u->list[i] = 1;
    dma_clean(u->list, 4096);
    PERIODIC = (uint32_t)(uintptr_t)u->list;
    *status = "USB AUDIO READY";
    return true;
}

bool usb_audio_pause(UsbAudio *u)
{
    if (!u || !u->owned || u->unsafe_dma)
        return false;
    /* A paused position query returns origin. Preserve the consumed position,
     * rather than the start of this run or the end of the queued audio. */
    uint64_t position = usb_audio_position(u);
    CMD &= ~PSE;
    u->running = false;
    if (!wait_status(u, PSS, 0)) {
        stop_schedules(u);
        return false;
    }
    u->origin = position;
    for (unsigned i = 0; i < RING; ++i)
        u->list[i] = 1;
    dma_clean(u->list, 4096);
    return true;
}
bool usb_audio_quiesce(UsbAudio *u)
{
    if (!u || !u->owned)
        return false;
    stage_event("USB quiesce schedules");
    bool safe = stop_schedules(u);
    u->running = false;
    return safe;
}
static bool update_frame(UsbAudio *u)
{
    uint32_t now = u->now_ms(), frame = (FRINDEX >> 3) & 2047;
    if ((uint32_t)(now - u->last_ms) > 1000) {
        u->error = "USB audio service stalled";
        return false;
    }
    u->frame += (frame - u->last_frame) & 2047;
    u->last_frame = frame;
    u->last_ms = now;
    return true;
}
#include "usb_audio_schedule.inc"

void usb_audio_volume(UsbAudio *u, unsigned volume)
{
    if (u)
    {
        u->volume = volume > 100 ? 100 : volume;
        u->gain_q15 = volume_gain_q15(u->volume);
    }
}
const char *usb_audio_error(const UsbAudio *u)
{
    return u ? u->error : "USB audio unavailable";
}
void usb_audio_describe(const UsbAudio *u, char *text, size_t capacity)
{
    if (!u) {
        snprintf(text, capacity, "%s", last_probe);
        return;
    }
    if (topology_probe && !u->format.endpoint) {
        snprintf(text, capacity, "Topology captured; playback not requested");
        return;
    }
    if (!u->format.endpoint) {
        snprintf(text, capacity, "%04x:%04x %s no usable playback format; declared power=%umA",
                 u->vendor, u->product, u->high_speed ? "HS" : "FS", u->max_power_ma);
        return;
    }
    snprintf(text, capacity, "%04x:%04x UAC%u %s %luHz %ubit %uch ep=%02x feedback=%02x power=%umA",
             u->vendor, u->product, u->format.protocol == 0x20 ? 2 : 1, u->high_speed ? "HS" : "FS",
             (unsigned long)u->format.sample_rate, u->format.sample_bits, u->format.channels,
             u->format.endpoint, u->format.feedback_endpoint, u->max_power_ma);
    size_t length = strlen(text);
    if (u->tt_hub && length < capacity)
        snprintf(text + length, capacity - length, " TT=%u/%u", u->tt_hub, u->tt_port);
}
bool usb_audio_close(UsbAudio *u)
{
    if (!u)
        return true;
    if (u->owned) {
        u->closing = true;
        stage_event("USB stop schedules");
        bool safe = stop_schedules(u);
        u->running = false;
        if (!safe)
            return false;
        if (safe && connected()) {
            stage_event("USB restore audio controls");
            /* Endpoint journals belong to the device we configured. Once it
             * stops responding, leave replacement enumeration to the native
             * owner instead of replaying writes through the stale route. */
            if (!u->link_lost && !rollback_format(u))
                u->link_lost = true;
            if (u->link_lost)
                topology_handoff_reason = "audio link lost";
            stage_event("USB restore hub ports");
            restore_hub_ports(u);
            safe = stop_schedules(u);
        }
        if (!safe)
            return false;
        if (safe) {
            if (u->media_cleanup) {
                u->media_cleanup(u->media);
                u->media = NULL;
                u->media_cleanup = NULL;
            }
            stage_event("USB restore native schedule");
            PERIODIC = u->saved_periodic;
            ASYNC = u->saved_async;
            /* Preserve port-change/OTG events so TI-OS observes hot unplug. */
            STS = ((1U << 0) | (1U << 1)) & ~u->saved_status;
            /* Doorbell/advance belongs to the paused native queue. Preserve
             * its completion and replay its original command on handback. */
            CMD = u->saved_command;
            INTR = u->saved_intr;
            stage_event("USB restore interrupt");
            if (u->saved_irq & u->irq_mask)
                IRQ_ENABLE = u->irq_mask;
        }
    }
    /* Keep the owner and DMA allocation alive until a retry proves it stopped. */
    stage_event("USB free audio DMA");
    if (!u->unsafe_dma)
        free_dma(u->allocation);
    stage_event("USB free owner");
    free(u);
    return true;
}
