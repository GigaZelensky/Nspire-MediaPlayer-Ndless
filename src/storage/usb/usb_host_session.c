#include "usb_host_session.h"
#include "player/app_task_io.h"
#include "player/native_usb_ui.h"
#include "player/native_interrupts.h"
#include "platform/nspire_hardware.h"
#include "storage/usb/usb_autoconnect.h"
#include <string.h>

#define WORD(address) (*(volatile uint32_t *)(uintptr_t)(address))
static bool leased;
static uint32_t saved_gate;
typedef struct {
    const char *stage;
    uint32_t cap, command, status, port, otg, gate, clock_gate, irq, presence;
    int wake, wait, reassert, lock;
    unsigned lock_stage;
    uint32_t native[8];
} HostSample;
static HostSample samples[12];
static unsigned sample_count;
static UsbRegisters registers;
static int wake_result, wait_result, reassert_result, lock_result;
static unsigned host_turns, host_stalls, host_wakes;
static uint32_t host_elapsed;
static void barrier(void)
{
    __asm__ volatile("mcr p15,0,%0,c7,c10,4" ::"r"(0) : "memory");
}
static void sample(const char *stage)
{
    unsigned mask = native_critical_enter();
    HostSample *s = samples + (sample_count < 12 ? sample_count++ : 11);
    *s = (HostSample){.stage = stage,
                      .wake = wake_result,
                      .wait = wait_result,
                      .reassert = reassert_result,
                      .lock = lock_result};
    s->cap = WORD(registers.capability);
    s->command = WORD(registers.command);
    s->status = WORD(registers.command + 4);
    s->port = WORD(registers.port);
    s->otg = WORD(registers.otg);
    s->gate = WORD(0x90140050U);
    s->clock_gate = WORD(0x90140060U);
    s->irq = WORD(0xdc000010U);
    s->presence = usb_auto_connect_presence(&registers, s->cap, s->cap, s->otg, s->port);
    native_usb_ui_bus_snapshot(s->native);
    s->lock_stage = native_usb_ui_bus_lock_stage();
    native_critical_leave(mask);
}
static bool ready(void)
{
    HostSample *s = samples + sample_count - 1;
    /* Electrical readiness alone can precede native start/resume completion.
     * The authenticated tree must also publish a running, registered host. */
    return s->native[0] == 1 && !s->native[2] && !s->native[3] &&
           s->native[4] && s->native[5] == 1 &&
           (s->cap & 0xffff00ffU) == 0x01000010U && !(s->otg & registers.role_mask) &&
           (s->port & 0x85U) == 5U && (s->command & 1U) &&
           !(s->command & (2U | 64U)) && !(s->status & (0x1000U | 16U));
}
/* Caller keeps IRQ/FIQ masked from the native return through this claim.
 * Otherwise a native worker can take the tree lock or suspend the bus in
 * between our readiness observation and semaphore acquisition. */
static int claim_ready(void)
{
    if (!ready()) {
        if (native_usb_ui_bus_is_leased()) {
            native_usb_ui_bus_release();
            return -1;
        }
        return 0;
    }
    lock_result = native_usb_ui_bus_is_leased() ? 0 : native_usb_ui_bus_acquire();
    sample("native schedule lease");
    if (lock_result)
        return 0;
    if (ready())
        return 1;
    native_usb_ui_bus_release();
    return -1;
}
bool usb_host_session_active(void)
{
    return leased && native_usb_ui_bus_is_leased();
}
void usb_host_session_release(void)
{
    if (!leased)
        return;
    native_usb_ui_bus_release();
    unsigned mask = native_critical_enter();
    uint32_t current = WORD(0x90140050U);
    WORD(0x90140050U) = (current & ~(1U << 5)) | (saved_gate & (1U << 5));
    barrier();
    leased = false;
    app_task_io_pause_service(false);
    native_critical_leave(mask);
}
bool usb_host_session_prepare(uint32_t (*now)(void), bool (*progress)(void *), void *context,
                            char *message, size_t capacity)
{
    if (leased) {
        sample("existing app bus lease");
        if (usb_host_session_active() && ready()) return true;
        /* Let the native worker finish a pending queue unlink/advance before
         * reusing its schedule. Waiting while holding the tree lease prevents
         * that worker from completing. Never clear its doorbell ourselves. */
        usb_host_session_release();
    }
    sample_count = 0;
    wake_result = wait_result = reassert_result = lock_result = 0;
    host_turns = host_stalls = host_wakes = 0;
    host_elapsed = 0;
    if (!nspire_is_cx2() || !usb_registers_current(&registers)) {
        snprintf(message, capacity, "Shared USB session needs a CX II");
        return false;
    }
    sample("before bus access");
    unsigned mask = native_critical_enter();
    saved_gate = WORD(0x90140050U);
    WORD(0x90140050U) = saved_gate & ~(1U << 5);
    barrier();
    leased = true;
    native_critical_leave(mask);
    sample("bus access enabled");
    uint32_t started = now(), resume_after = started + 250U, last_progress = started;
    unsigned resumes = 0;
    /* A scheduler turn is not a duration. Keep the fallback only for a clock
     * that stops progressing, so legitimate fast ticks get the full deadline. */
    while (host_stalls < 500) {
        uint32_t current = now();
        host_elapsed = current - started;
        if (host_elapsed >= 8000U)
            break;
        if (current != last_progress) {
            host_stalls = 0;
            last_progress = current;
        } else
            ++host_stalls;
        ++host_turns;
        if (progress && !progress(context)) {
            snprintf(message, capacity, "Cancelled");
            break;
        }
        native_critical_enter();
        wait_result = native_usb_ui_wait();
        native_critical_enter();
        reassert_result = native_usb_ui_reassert();
        sample("native enumeration wait");
        if (wait_result || reassert_result) {
            snprintf(message, capacity, "USB wake failed: wait %d, UI %d", wait_result,
                     reassert_result);
            break;
        }
        int claimed = claim_ready();
        if (claimed > 0) {
            app_task_io_pause_service(true);
            host_elapsed = now() - started;
            native_critical_leave(0);
            return true;
        }
        if (claimed < 0) {
            snprintf(message, capacity, "USB connection changed before probe");
            break;
        }
        native_critical_leave(0);
        HostSample *s = samples + sample_count - 1;
        if ((int32_t)(now() - resume_after) >= 0 && resumes < 4 &&
            s->presence != USB_AUTO_UNKNOWN && (s->presence & USB_AUTO_ADAPTER)) {
            native_critical_enter();
            ++host_wakes;
            wake_result = native_usb_ui_resume_host_lease();
            native_critical_enter();
            reassert_result = native_usb_ui_reassert();
            /* NU_UNAVAILABLE is a lock-contention result, not a hardware wake.
             * Don't spend every wake attempt while native teardown is busy. */
            if (wake_result != -51)
                ++resumes;
            resume_after = now() + 500U;
            sample("guarded host start/resume");
            if (reassert_result) {
                snprintf(message, capacity, "USB wake failed: UI %d", reassert_result);
                break;
            } else {
                claimed = claim_ready();
                if (claimed > 0) {
                    app_task_io_pause_service(true);
                    host_elapsed = now() - started;
                    native_critical_leave(0);
                    return true;
                }
                if (claimed < 0) {
                    snprintf(message, capacity, "USB connection changed before probe");
                    break;
                }
            }
            native_critical_leave(0);
        }
    }
    host_elapsed = now() - started;
    native_critical_leave(0);
    if (!message[0] || !strcmp(message, "Preparing headset connection...")) {
        HostSample *s = samples + sample_count - 1;
        if (lock_result)
            snprintf(message, capacity, "Native USB bus busy (%d). Try again.", lock_result);
        else if (s->presence == USB_AUTO_UNKNOWN)
            snprintf(message, capacity, "USB state unreadable; host wake %d", wake_result);
        else if (!(s->presence & USB_AUTO_ADAPTER))
            snprintf(message, capacity, "OTG ID not detected (device mode)");
        else if (!(s->port & 1U))
            snprintf(message, capacity, "OTG detected; no attached device. Wake %d", wake_result);
        else
            snprintf(message, capacity, "USB attached but not enabled. Wake %d", wake_result);
    }
    usb_host_session_release();
    return false;
}
void usb_host_session_debug(FILE *file)
{
    fprintf(file, "audio_host model=%08lx samples=%u leased=%u saved_gate=%08lx\n",
            (unsigned long)nspire_hardware_id(), sample_count, leased, (unsigned long)saved_gate);
    fprintf(file, "native_usb_irq_mask=%08lx\n", (unsigned long)native_usb_ui_bus_irq_mask());
    fprintf(file, "host_wait elapsed_ms=%lu turns=%u consecutive_clock_stalls=%u wake_calls=%u\n",
            (unsigned long)host_elapsed, host_turns, host_stalls, host_wakes);
    for (unsigned i = 0; i < sample_count; ++i) {
        HostSample *s = samples + i;
        fprintf(
            file,
            "host stage=%s cap=%08lx cmd=%08lx sts=%08lx port=%08lx otg=%08lx presence=%08lx "
            "gate=%08lx clock_gate=%08lx irq=%08lx wake=%d wait=%d UI=%d lock=%d lock_stage=%u\n",
            s->stage, (unsigned long)s->cap, (unsigned long)s->command, (unsigned long)s->status,
            (unsigned long)s->port, (unsigned long)s->otg, (unsigned long)s->presence,
            (unsigned long)s->gate, (unsigned long)s->clock_gate, (unsigned long)s->irq, s->wake,
            s->wait, s->reassert, s->lock, s->lock_stage);
        fprintf(file,
                "host_native stage=%s valid=%lu saved_cmd=%08lx suspended=%lu handle_suspended=%lu "
                "irq=%08lx active=%lu polling=%lu children=%lu\n",
                s->stage, (unsigned long)s->native[0], (unsigned long)s->native[1],
                (unsigned long)s->native[2], (unsigned long)s->native[3],
                (unsigned long)s->native[4], (unsigned long)s->native[5],
                (unsigned long)s->native[6], (unsigned long)s->native[7]);
    }
    native_usb_ui_debug(file);
}
