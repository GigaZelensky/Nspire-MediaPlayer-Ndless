#include "player_data.h"
#include "usb_browser.h"
#include "../storage/usb/usb_registers.h"
#include "../storage/usb/usb_autoconnect.h"
#include "native_usb_ui.h"
#include "native_writer_platform.h"
#include "native_interrupts.h"
#include "performance_clock.h"
#include "raw_player_io.h"
#include "app_task_io.h"
#include "storage_activity.h"
#include "storage_mutation.h"
#include "../storage/usb/usb_storage.h"
#include "../storage/usb/usb_fs_worker.h"
#include "../storage/usb/usb_controller.h"
#include "../storage/usb/usb_host_session.h"
#include "../sram.h"
#include "media_browser.h"
#include <errno.h>

extern bool player_standby_guard_active(void);
static bool standby_was_outer;
void usb_browser_reserve_native_ui_order(void)
{if(!native_usb_ui_active())standby_was_outer=player_standby_guard_active();}
static UsbAutoConnect auto_connection;
static bool last_preconnected;
static bool connection_attempted;
static bool quiet_reconnect;
static LoadingProgress *retry_progress;
static bool last_connection_cancelled;
static bool exit_audit_enabled;
static uint32_t discovery_after;
static unsigned discovery_turns;
static int discovery_result;
static int discovery_prepared;
static bool last_attachment_ready;
static uint32_t last_connection_ms;
static struct {
    uint32_t samples, unknown, insertions, cap, identity, otg, port;
    uint32_t attempt_cap, attempt_identity, attempt_otg, attempt_port;
    unsigned presence;
    unsigned attempt_presence;
} detection;
static unsigned resume_attempts;
static int resume_result;
static const char *connection_stage = "idle";
static bool io_transition;
static SDL_Surface *io_screen;
typedef struct {bool release,ok;char error[160];} UsbIoTransition;
static bool io_progress(void *context)
{
    (void)context;
    if(io_screen)present_screen(io_screen);
    return !isKeyPressed(KEY_NSPIRE_ESC);
}
static void io_native(void *context)
{
    UsbIoTransition *operation=context;
    unsigned mask=native_critical_enter(),policy=0;
    const volatile uint32_t *counter;uint32_t hz;
    bool clock=false,enabled=false;
    if(!native_usb_ui_clock(&counter,&hz,io_progress,NULL) ||
       !monotonic_clock_enter_native(counter,hz))goto done;
    clock=true;policy=TCT_Local_Control_Interrupts(0);enabled=true;
    native_critical_leave(0);
    if(operation->release) {usb_host_session_release();operation->ok=true;}
    else operation->ok=usb_host_session_prepare(monotonic_clock_now_ms,io_progress,NULL,
        operation->error,sizeof(operation->error));
done:
    native_critical_enter();
    if(clock)monotonic_clock_leave_native();
    if(enabled)TCT_Local_Control_Interrupts(policy);
    native_critical_leave(mask);
}
void usb_browser_release_io(void)
{
    if(io_transition || !usb_host_session_active())return;
    io_transition=true;
    while(!raw_player_prepare_native()) {}
    UsbIoTransition operation={.release=true};
    if(!sram_with_native_mapping(io_native,&operation) || !operation.ok)
        usb_controller_set_error("Could not release USB bus ownership.",EIO);
    raw_player_native_released();io_transition=false;
}
static int prepare_filesystem_io(void *context)
{
    (void)context;
    if(!nspire_is_cx2() || !usb_controller_online() || usb_host_session_active())return 1;
    if(io_transition)return 0;
    /* A suspended private NAND writer must finish before any OS scheduling.
     * This hook runs on the foreground stack, never inside the FatFs fiber. */
    if(app_task_io_pending() || storage_native_active()) {app_task_io_service(32);return 0;}
    if(!raw_player_prepare_native()) {raw_player_native_released();return 0;}
    io_transition=true;
    UsbIoTransition operation={0};
    bool mapped=sram_with_native_mapping(io_native,&operation);
    raw_player_native_released();io_transition=false;
    if(!mapped || !operation.ok) {
        usb_controller_set_error(operation.error[0]?operation.error:"Could not acquire USB bus ownership.",EIO);
        return -1;
    }
    return 1;
}

static uint32_t connection_seconds(void)
{
    return *(const volatile uint32_t *)0x90090000U;
}

static bool sample_usb_registers(UsbRegisters *r, uint32_t *cap, uint32_t *identity,
                                 uint32_t *otg, uint32_t *port)
{
    if (!usb_registers_current(r)) return false;
    unsigned mask = native_critical_enter();
    volatile uint32_t *gate = (volatile uint32_t *)(uintptr_t)
        (r->classic ? 0x900b0018U : 0x90140050U);
    uint32_t saved_gate = *gate;
    if (saved_gate & (1U << 5)) {
        *gate = saved_gate & ~(1U << 5);
        __asm__ volatile("mcr p15,0,%0,c7,c10,4" :: "r"(0) : "memory");
    }
    uint32_t started = *(const volatile uint32_t *)0x900c0004U;
    unsigned polls = 0;
    do {
        *cap = *(const volatile uint32_t *)(uintptr_t)r->capability;
        ++polls;
    } while ((*cap & 0xffff00ffU) != (r->classic ? 0x01000040U : 0x01000010U) &&
             started - *(const volatile uint32_t *)0x900c0004U < 16U && polls < 65536U);
    *identity = r->classic ? *(const volatile uint32_t *)0xb0000000U : *cap;
    *otg = *(const volatile uint32_t *)(uintptr_t)r->otg;
    *port = *(const volatile uint32_t *)(uintptr_t)r->port;
    if (saved_gate & (1U << 5)) {
        *gate = saved_gate;
        __asm__ volatile("mcr p15,0,%0,c7,c10,4" :: "r"(0) : "memory");
    }
    native_critical_leave(mask);
    return true;
}
static bool host_device_present(void)
{
    UsbRegisters r;
    return usb_registers_current(&r) && usb_registers_valid(&r) &&
        !(*(const volatile uint32_t *)(uintptr_t)r.otg & r.role_mask) &&
        (*(const volatile uint32_t *)(uintptr_t)r.port & 1U);
}
static unsigned connection_presence(void)
{
    UsbRegisters r;
    uint32_t cap, identity, otg, port;
    if (!sample_usb_registers(&r, &cap, &identity, &otg, &port)) return USB_AUTO_UNKNOWN;
    return usb_auto_connect_presence(&r, cap, identity, otg, port);
}
bool usb_browser_device_present(void)
{
    unsigned presence=connection_presence();
    return presence!=USB_AUTO_UNKNOWN && (presence&USB_AUTO_DEVICE);
}
bool usb_browser_adapter_present(void)
{
    unsigned presence=connection_presence();
    return presence!=USB_AUTO_UNKNOWN && (presence&USB_AUTO_ADAPTER);
}
static uint32_t host_connection_state(void)
{
    UsbRegisters r;
    if (!usb_registers_current(&r) || !usb_registers_valid(&r)) return 0;
    /* Ignore interrupt/status-change bits: retry for actual run, halt, connect,
     * enable or suspend transitions, not for routine interrupt traffic. */
    return (*(const volatile uint32_t *)(uintptr_t)r.command & 3U) |
        ((*(const volatile uint32_t *)(uintptr_t)(r.command + 4) & 0x1000U) >> 10) |
        ((*(const volatile uint32_t *)(uintptr_t)r.port & 0x85U) << 8);
}
void usb_browser_note_disconnect(void)
{
    usb_auto_connect_removed(&auto_connection,connection_presence(),monotonic_clock_now_ms());
    last_attachment_ready=false;
    discovery_after=0;
}
static bool discovery_progress(void *context)
{
    present_screen(context);
    return !isKeyPressed(KEY_NSPIRE_ESC);
}
static void discovery_native(void *context)
{
    unsigned mask = native_critical_enter();
    const volatile uint32_t *counter;
    uint32_t hz;
    bool native_clock = false, native_policy = false;
    unsigned policy = 0;
    discovery_result = -23;
    if (!native_usb_ui_clock(&counter, &hz, discovery_progress, context) ||
        !monotonic_clock_enter_native(counter, hz)) goto done;
    native_clock = true;
    policy = TCT_Local_Control_Interrupts(0);
    native_critical_enter();
    native_policy = true;
    if (!native_usb_ui_active()) standby_was_outer = player_standby_guard_active();
    discovery_result = native_usb_ui_acquire(discovery_progress, context);
    if (!discovery_result) {
        /* The OS owns PHY power and the deferred role-change worker. Give a
         * pending insertion interrupt/task one native tick with its real
         * timer, SRAM and display reservation. No driver is registered and
         * no port is reset here; hardware state decides whether to connect. */
        usb_storage_watch_attachments();
        discovery_result = native_usb_ui_wait();
        native_critical_enter();
        if (!discovery_result) discovery_result = native_usb_ui_reassert();
    }
done:
    if (native_policy) {
        TCT_Local_Control_Interrupts(policy);
        native_critical_enter();
    }
    if(native_clock){performance_clock_start();monotonic_clock_leave_native();}
    monotonic_clock_restore_player_timer();
    native_critical_leave(mask);
}
bool usb_browser_auto_pending(uint32_t now)
{
    if(!NDMEDIA_USB_STORAGE)return false;
    UsbRegisters r;
    unsigned presence = USB_AUTO_UNKNOWN;
    if (sample_usb_registers(&r, &detection.cap, &detection.identity,
                             &detection.otg, &detection.port)) {
        presence = usb_auto_connect_presence(&r, detection.cap, detection.identity, detection.otg, detection.port);
    }
    detection.presence = presence;
    ++detection.samples;
    if (presence == USB_AUTO_UNKNOWN) ++detection.unknown;
    bool ready=!usb_storage_mounted() && usb_storage_attachment_ready();
    if(ready && !last_attachment_ready)usb_auto_connect_removed(&auto_connection,presence,now);
    last_attachment_ready=ready;
    bool pending = usb_auto_connect_poll(&auto_connection, presence, now, usb_storage_mounted());
    if (pending) ++detection.insertions;
    return pending;
}
void usb_browser_idle_discovery(SDL_Surface *screen, uint32_t now)
{
    if(!NDMEDIA_USB_STORAGE)return;
    /* Called only after a complete picker frame/entrance has been painted.
     * The event poll above never resolves firmware bindings or sleeps. */
    bool watch=detection.presence==USB_AUTO_UNKNOWN || (detection.presence&USB_AUTO_ADAPTER);
    if (watch && discovery_prepared == 0) {
        /* Read-only resolution uses the validated current mapping (including
         * the native SRAM clone). It neither invokes OS functions nor drains
         * metadata jobs; a suspended native writer is allowed to finish first. */
        if (!storage_native_active()) discovery_prepared = native_usb_ui_prepare_step();
        /* One bounded slice per picker frame. Return to input/rendering even
         * when this slice just finished preparation. */
        return;
    }
    if(usb_storage_mounted()){
        for(size_t i=0;i<g_picker_cache.count;++i){
            const MovieFile *f=g_picker_cache.files+i;
            if((f->kind==PICKER_MOVIE && !f->timing_checked) ||
               (f->kind==PICKER_FOLDER && !f->folder_count_checked))return;
        }
    }
    if (discovery_prepared > 0 && watch &&
        (int32_t)(now - discovery_after) >= 0) {
        /* Background work must never force a foreground join of file jobs. */
        if (app_task_io_pending() || storage_native_active() || !usb_storage_quiescent()) return;
        if (!raw_player_prepare_native()) {
            raw_player_native_released();
            return;
        }
        usb_browser_release_io();
        storage_mutation_invalidate();
        ++discovery_turns;
        if (!sram_with_native_mapping(discovery_native, screen)) discovery_result = -20;
        raw_player_native_released();
        now = monotonic_clock_now_ms();
        discovery_after = now + (discovery_result ? 2000U : 750U);
        detection.presence = connection_presence();
    }
}
bool usb_browser_recovery_poll(SDL_Surface *screen)
{
    if(app_task_io_pending() || storage_native_active()) {
        app_task_io_service(32U);
        return false;
    }
    if(!usb_storage_quiescent())return false;
    if(!raw_player_prepare_native()) {
        raw_player_native_released();
        return false;
    }
    usb_browser_release_io();
    storage_mutation_invalidate();
    ++discovery_turns;
    if(!sram_with_native_mapping(discovery_native,screen))discovery_result=-20;
    raw_player_native_released();
    return discovery_result==0;
}
void usb_browser_debug(FILE *file)
{
    if (file) fprintf(file, "usb_connect preconnected=%u stage=%s auto_state=%u auto_candidate=%u resume_attempts=%u resume_result=%d\n",
                      last_preconnected, connection_stage, auto_connection.stable,
                      auto_connection.candidate, resume_attempts, resume_result);
    if (file) fprintf(file, "usb_detection samples=%lu unknown=%lu insertions=%lu presence=%08x "
                      "cap=%08lx identity=%08lx otg=%08lx port=%08lx\n",
                      (unsigned long)detection.samples, (unsigned long)detection.unknown,
                      (unsigned long)detection.insertions, detection.presence,
                      (unsigned long)detection.cap, (unsigned long)detection.identity,
                      (unsigned long)detection.otg, (unsigned long)detection.port);
    if (file) fprintf(file, "usb_detection_before_attempt presence=%08x cap=%08lx identity=%08lx "
                      "otg=%08lx port=%08lx\n",
                      detection.attempt_presence, (unsigned long)detection.attempt_cap,
                      (unsigned long)detection.attempt_identity, (unsigned long)detection.attempt_otg,
                      (unsigned long)detection.attempt_port);
    if (file) fprintf(file, "usb_discovery prepared=%d turns=%u result=%d\n", discovery_prepared, discovery_turns, discovery_result);
    if(file)fprintf(file,"usb_last_connection_ms=%lu\n",(unsigned long)last_connection_ms);
    if(file)usb_host_session_debug(file);
}
typedef struct {
    SDL_Surface *screen;
    const Fonts *fonts;
    PointerState *pointer;
    MessageBackdropDraw backdrop;
    void *backdrop_context;
    UiTransition hover, press;
    uint32_t opened, closed, last_draw;
    bool closing, cancelled, connected, previous_enter, previous_escape, armed, armed_pointer;
    bool timed_out, preconnected, checkpoint_enabled, checkpoint_started, audio_only;
    bool automatic, had_storage;
    uint8_t last_mix,closing_mix;
    const char *line1, *line2, *status;
    int guard_error;
} UsbConnectWindow;
static void connection_checkpoint(UsbConnectWindow *window, const char *stage, bool fresh)
{
    connection_stage = stage;
    if (!window->checkpoint_enabled) return;
    unsigned mask = native_critical_enter();
    char path[MAX_PATH_LEN];
    fresh = fresh || !window->checkpoint_started;
    if (player_data_prepare() && player_data_path("ndvideo-usb.log.tns", path, sizeof(path))) {
        bool append = !fresh;
        if (fresh) {
            FILE *previous = fopen(path, "rb");
            if (previous) {
                fclose(previous);
                append = true;
            }
        }
        FILE *file = fopen(path, append ? "ab" : "wb");
        if (file) {
            window->checkpoint_started = true;
            if (fresh) fprintf(file, "nMedia Player USB connection | %s %s\n", __DATE__, __TIME__);
            fprintf(file, "STAGE: %s at_ms=%lu\n", stage, (unsigned long)monotonic_clock_now_ms());
            usb_storage_debug(file);
            native_usb_ui_debug(file);
            monotonic_clock_debug(file);
            native_writer_debug(file);
            usb_browser_debug(file);
            fclose(file);
        }
    }
    native_critical_leave(mask);
}
static bool render_retry(UsbConnectWindow *window)
{
    if (!quiet_reconnect || !retry_progress)return false;
    /* The recovery owner draws and polls one persistent scene through
     * storage enumeration and audio setup, including a latched Cancel. */
    loading_progress_tick(retry_progress, true);
    if (isKeyPressed(KEY_NSPIRE_ESC) || (retry_progress->cancelled &&
        retry_progress->cancelled(retry_progress->context))) {
        window->cancelled = true;
        usb_fs_cancel_active();
    }
    native_critical_enter();
    return true;
}
static void render_window(UsbConnectWindow *window, bool force)
{
    uint32_t now = monotonic_clock_now_ms();
    if (!force && window->last_draw && now - window->last_draw < 16U)
        return;
    window->last_draw = now;
    if (render_retry(window))return;
    PointerState *pointer = window->pointer;
    pointer_update(pointer);
    bool enter = isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_RET) ||
                 isKeyPressed(KEY_NSPIRE_5);
    bool escape = isKeyPressed(KEY_NSPIRE_ESC);
    uint32_t age=now-window->opened;
    uint8_t mix=window->closing?window->closing_mix*(255U-ui_ease_out_cubic(now-window->closed,160))/255U:
        age<120U?0:ui_ease_out_cubic(age-120U,120);
    window->last_mix=mix;
    SDL_Rect panel = {30, 60 + (255 - mix) * 12 / 255, 260, 128},
             button = {124, panel.y + 94, 72, 23};
    bool hot = pointer->visible && rect_contains_point(&button, pointer->x, pointer->y);
    if (!window->closing) {
        if ((enter && !window->previous_enter) || (escape && !window->previous_escape))
            window->armed = true;
        if (pointer->press_edge && hot)
            window->armed_pointer = true;
        if (window->armed_pointer && !hot)
            window->armed_pointer = false;
        if ((window->armed && !enter && !escape) || (window->armed_pointer && !pointer->down)) {
            window->cancelled = true;
            usb_fs_cancel_active();
            ui_transition_begin_press_release(&window->press, now);
        }
    }
    window->previous_enter = enter;
    window->previous_escape = escape;
    uint8_t hover =
        ui_transition_update(&window->hover, hot && !window->closing, now, UI_HOVER_ANIM_MS);
    uint8_t press = ui_transition_update_press_ex(
        &window->press, !window->closing && (window->armed || window->armed_pointer), now,
        UI_PRESS_ANIM_MS, PICKER_PRESS_RELEASE_ANIM_MS);
    if (window->backdrop)
        window->backdrop(window->screen, window->backdrop_context,
                         window->closing ? now - window->closed : 0);
    else
        SDL_FillRect(window->screen, NULL, 0);
    draw_overlay_backdrop_dim(window->screen, mix);
    if (mix) {
        SDL_Surface *target;
        SDL_Rect clip;
        int dx, dy;
        SDL_Surface *layer =
            begin_faded_region_draw(window->screen, &panel, mix, &target, &clip, &dx, &dy);
        SDL_Rect box = offset_sdl_rect(&panel, dx, dy), action = offset_sdl_rect(&button, dx, dy);
        draw_soft_glass_panel_body_from_y(target, &box, box.y, UI_COLOR_GUNMETAL);
        int inset = soft_panel_inset_for_row(0, box.h);
        SDL_Rect accent = {box.x + inset, box.y, box.w - 2 * inset, 1};
        fill_rect_rgb565(target, &accent, UI_COLOR_ACCENT);
        const char *lines[] = {"USB", window->line1, window->line2, window->status};
        const int ys[] = {13, 40, 54, 75};
        for (unsigned i = 0; i < 4; ++i)
            if (lines[i]) {
                char fitted[80];
                copy_fitted_text(window->fonts->white, lines[i], fitted, sizeof(fitted),
                                 box.w - 20);
                int width = nSDL_GetStringWidth(window->fonts->white, fitted);
                size_t length = strlen(fitted);
                if (i == 3 && length >= 3 && !strcmp(fitted + length - 3, "..."))
                    fitted[length - 3 + ((now / 180U) % 3U) + 1U] = 0;
                draw_ui_label(target, window->fonts, box.x + (box.w - width) / 2, box.y + ys[i],
                              fitted);
            }
        draw_prompt_button(target, window->fonts, &action, "Cancel", hover, press);
        draw_soft_glass_panel_rim(target, &action, UI_COLOR_GUNMETAL, 255);
        end_faded_region_draw(window->screen, layer, &panel, &clip, mix);
    }
    if (pointer->visible)
        draw_cursor(window->screen, pointer->x, pointer->y);
    present_screen(window->screen);
    native_critical_enter();
}
static void progress(void *context)
{
    /* The worker bounds callback frequency, including when its timer stalls.
     * Do not skip input polling just because UI time stopped advancing. */
    render_window(context, true);
}
static bool prepare_progress(void *context)
{
    progress(context);
    return !((UsbConnectWindow *)context)->cancelled;
}
static void connect_native(void *context)
{
    UsbConnectWindow *window = context;
    resume_attempts = 0;
    resume_result = 0;
    unsigned saved = native_critical_enter();
    unsigned kernel_mask = 0;
    bool native_enabled = false;
    const volatile uint32_t *os_counter;
    uint32_t os_hz;
    /* Display control, file I/O and driver registration can service native
     * interrupts or yield. A fresh insertion may already have OS USB work
     * pending: restore its clock/policy BEFORE any of those calls, not just
     * before our explicit enumeration wait. No display/file ownership yet. */
    if (!native_usb_ui_clock(&os_counter, &os_hz, prepare_progress, window) ||
        !monotonic_clock_enter_native(os_counter, os_hz)) {
        window->guard_error = -23;
        goto done;
    }
    kernel_mask = TCT_Local_Control_Interrupts(0);
    native_critical_enter();
    native_enabled = true;
    if (!native_usb_ui_active())
        standby_was_outer = player_standby_guard_active();
    window->guard_error = native_usb_ui_acquire(prepare_progress, window);
    if (window->guard_error) {
        connection_checkpoint(window, "native UI reservation failed", true);
        debug_tracef("USB UI refused stage=%s error=%d", native_usb_ui_stage(),
                     window->guard_error);
        native_usb_ui_release();
        goto done;
    }
    if (window->cancelled)
        goto done;
    connection_checkpoint(window, "before filesystem worker allocation", false);
    if (!usb_fs_prepare()) {
        window->guard_error = -25;
        goto done;
    }
    connection_checkpoint(window, "filesystem worker allocated", false);
    /* Native file writes may run OS work. Take display ownership before the
     * first checkpoint, then repaint before doing storage initialization. */
    render_window(window, true);
    connection_checkpoint(window, "display reserved", true);
    /* Playback prepares its recording writer in the movie's loading window.
     * USB enumeration does not use it; do not delay the connection with that
     * separate firmware scan. Ordinary checkpoint writes stay synchronous. */
    if (window->cancelled) goto done;
    bool prepared = usb_storage_prepare_connection();
    if (!prepared)
        goto done;
    connection_checkpoint(window, "driver registered", false);
    window->status = "Detecting the USB drive...";
    uint32_t entered = monotonic_clock_now_ms();
    uint32_t rtc_entered = connection_seconds();
    unsigned registrations = 1;
    connection_checkpoint(window, "waiting for attachment", false);
    bool sampled = false, host_resumed = false;
    /* A newly inserted adapter may still be bringing up its native HCD.
     * Give that worker a chance before manually starting the same cold bus. */
    uint32_t resume_after = entered + (window->preconnected ? 250U : 1000U);
    uint32_t present_since = 0;
    uint32_t observed_state = host_connection_state(), attempted_state = observed_state;
    while (!window->cancelled) {
        native_critical_enter();
        if (!usb_storage_driver_registered()) {
            if (registrations++ >= 3 || !usb_storage_prepare_connection())
                break;
            window->status = "Detecting the USB drive...";
        }
        uint32_t now = monotonic_clock_now_ms();
        bool present = host_device_present();
        unsigned presence = connection_presence();
        if (presence == USB_AUTO_UNKNOWN) presence = 0;
        window->line1 = present ? "USB device connected." :
            presence & USB_AUTO_ADAPTER ? "OTG adapter connected." :
            "Connect an OTG adapter and USB drive.";
        window->line2 = present ? "Checking the connection." :
            presence & USB_AUTO_ADAPTER ? "Waiting for a USB drive." :
            "They can be connected together.";
        uint32_t host_state = host_connection_state();
        if (host_state != observed_state) {
            observed_state = host_state;
            resume_after = now + (window->preconnected ? 500U : 1000U);
        }
        if (present && !present_since) present_since = now;
        if (!present) present_since = 0;
        if(present && now-entered>=250U && usb_controller_audio_only()) {
            window->audio_only=true;break;
        }
        if (now - entered >= (window->preconnected ? 8000U : 30000U) ||
            connection_seconds() - rtc_entered >= (window->preconnected ? 9U : 31U)) {
            window->timed_out = true;
            usb_controller_set_error(
                window->preconnected
                    ? "The connected USB drive could not be opened."
                    : "No USB drive detected. Check the adapter and drive, then press U to retry.", ENODEV);
            break;
        }
        if (usb_storage_attachment_ready() && present) {
            window->had_storage = true;
            window->status = "Opening the USB drive...";
            { /* The completed native attachment callback is the readiness boundary. */
                render_window(window, true);
                connection_checkpoint(window, "attachment ready", false);
                /* Pipe open/close is native OS work. Only SCSI and filesystem
                 * reads use our polled timer. Switch at each idle candidate
                 * boundary, including after an empty reader or bad volume. */
                while(!window->cancelled && usb_storage_next_candidate()) {
                    bool endpoints=usb_storage_prepare_endpoints();
                    native_critical_enter();
                    window->guard_error=native_usb_ui_reassert();
                    if(window->guard_error)break;
                    connection_checkpoint(window, "storage endpoints ready", false);
                    if(endpoints && nspire_is_cx2()) {
                        char message[160]="";
                        if(!usb_host_session_prepare(monotonic_clock_now_ms,prepare_progress,window,message,sizeof(message))) {
                            usb_controller_set_error(message[0]?message:"Could not acquire the USB bus.",EIO);
                            break;
                        }
                        native_critical_enter();
                    }
                    monotonic_clock_leave_native();
                    TCT_Local_Control_Interrupts(kernel_mask);
                    native_critical_enter();
                    native_enabled = false;
                    if (!monotonic_clock_restore_player_timer()) {
                        window->guard_error = -24;
                        break;
                    }
                    if(endpoints && usb_storage_finish_connection()) {
                        window->status = "Reading folders...";
                        render_window(window, true);
                        int error = 0;
                        usb_fs_set_progress(progress, window);
                        window->connected = usb_volume_mount(&error);
                        usb_fs_set_progress(NULL, NULL);
                    }
                    if(window->connected || window->cancelled)break;
                    if(!monotonic_clock_enter_native(os_counter,os_hz)) {
                        window->guard_error=-24;break;
                    }
                    TCT_Local_Control_Interrupts(0);native_critical_enter();native_enabled=true;
                    usb_host_session_release();
                }
                if (window->connected || window->cancelled || window->guard_error)
                    break;
                /* An empty reader may enumerate before a populated sibling.
                 * Keep accepting native callbacks until a usable volume or
                 * the deadline, and reoffer any previously unclaimed child. */
                host_resumed = false;
                    resume_after = monotonic_clock_now_ms() + 250U;
            }
        } else {
            window->status = present ? "Detecting the USB drive..." : "Waiting for a USB drive...";
            if ((!host_resumed || !(host_state & 1U) || host_state != attempted_state) &&
                (resume_attempts < 4 || resume_result == -51) &&
                (int32_t)(now - resume_after) >= 0 && (presence & USB_AUTO_ADAPTER) &&
                (window->preconnected || !present || now - present_since >= 1000U)) {
                bool trace_recovery = resume_attempts == 0 || resume_result != -51 ||
                    host_state != attempted_state;
                int previous_result = resume_result;
                if (trace_recovery) connection_checkpoint(window, "before existing EHCI recovery", false);
                attempted_state = host_state;
                resume_result = native_usb_ui_resume_host();
                ++resume_attempts;
                host_resumed = resume_result == 0;
                resume_after = monotonic_clock_now_ms() + 250U;
                window->guard_error = native_usb_ui_reassert();
                render_window(window, true);
                /* Lock contention performs no recovery operation. Keep trying
                 * until the window deadline, without filling flash with the
                 * same checkpoint on every short retry. */
                if (trace_recovery || resume_result != previous_result)
                    connection_checkpoint(window, "existing EHCI recovery returned", false);
                if (window->guard_error) break;
            }
            if (!sampled && present && now - present_since >= 2000U) {
                connection_checkpoint(window, "attachment still pending", false);
                sampled = true;
            }
        }
        render_window(window, true);
        /* A suspended preconnected host may have a worker holding the tree
         * lock. Polling interrupts leaves that worker blocked forever and
         * every guarded resume returns NU_UNAVAILABLE. At this native-clock,
         * native-mapping boundary yield a real scheduler tick, as the tested
         * audio host preparation does, before retrying ownership. */
        window->guard_error = native_usb_ui_wait();
        if (!window->guard_error)
            window->guard_error = native_usb_ui_reassert();
        if (window->guard_error)
            break;
    }
    if(usb_host_session_active()) {
        if(!native_enabled) {
            if(!monotonic_clock_enter_native(os_counter,os_hz)) {window->guard_error=-24;goto done;}
            TCT_Local_Control_Interrupts(0);native_critical_enter();native_enabled=true;
        }
        usb_host_session_release();native_critical_enter();
    }
    monotonic_clock_leave_native();
    usb_storage_stop_accepting();
    while (!usb_storage_park()) {
    }
    TCT_Local_Control_Interrupts(kernel_mask);
    native_critical_enter();
    native_enabled = false;
    /* Native USB startup may select its charging clock. Reapply the user's
     * selected rate once, here at the native mapping and idle-I/O boundary. */
    if (!performance_clock_start())
        debug_tracef("USB clock restore failed status=%d", performance_clock_status());
    usb_storage_unpark();
done:
    connection_checkpoint(window, window->connected ? "connected" : window->cancelled ? "cancelled" : "connection ended", false);
    monotonic_clock_leave_native();
    usb_storage_stop_accepting();
    if (native_enabled) {
        native_critical_enter();
        TCT_Local_Control_Interrupts(kernel_mask);
        native_critical_enter();
        if (!performance_clock_start())
            debug_tracef("USB clock restore failed status=%d", performance_clock_status());
    }
    /* Checkpoint/native cleanup is also outside a live storage command. */
    monotonic_clock_restore_player_timer();
    native_critical_leave(saved);
}
static bool storage_connection_failed(const UsbConnectWindow *window)
{
    /* An unsolicited scan may see a headset or another non-storage device.
     * Absence of a storage candidate is ordinary in that case. Manual scans,
     * controller failures and failures opening a witnessed drive still report. */
    return !window->cancelled && !window->connected && !window->audio_only &&
           !(window->automatic && window->timed_out && !window->had_storage && !window->guard_error);
}
static bool connect_storage(SDL_Surface *screen, const Fonts *fonts, PointerState *pointer,
                            MessageBackdropDraw backdrop, void *context, bool automatic)
{
    if(!NDMEDIA_USB_STORAGE)return false;
    io_screen=screen;
    usb_fs_set_prepare(prepare_filesystem_io,NULL);
    connection_attempted = true;
    detection.attempt_presence = detection.presence;
    detection.attempt_cap = detection.cap;
    detection.attempt_identity = detection.identity;
    detection.attempt_otg = detection.otg;
    detection.attempt_port = detection.port;
    usb_auto_connect_acknowledge(&auto_connection, connection_presence(), monotonic_clock_now_ms());
    if (usb_storage_mounted()) return true;
    raw_player_before_native();
    last_preconnected = host_device_present();
    UsbConnectWindow window = {.screen = screen,
                               .automatic = automatic,
                               .preconnected = last_preconnected,
                               .checkpoint_enabled = !quiet_reconnect &&
                                   (debug_is_runtime_logging_enabled() || isKeyPressed(KEY_NSPIRE_D)),
                               .fonts = fonts,
                               .pointer = pointer,
                               .backdrop = backdrop,
                               .backdrop_context = context,
                               .line1 = "Preparing the USB connection.",
                               .line2 = "",
                               .status = "Starting USB...",
                               .opened = monotonic_clock_now_ms(),
                               .previous_enter = isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_RET) ||
                                                 isKeyPressed(KEY_NSPIRE_5),
                               .previous_escape = isKeyPressed(KEY_NSPIRE_ESC)};
    exit_audit_enabled = exit_audit_enabled || window.checkpoint_enabled;
    if (!sram_with_native_mapping(connect_native, &window))
        window.guard_error = -20;
    if (!quiet_reconnect && storage_connection_failed(&window)) {
        int error = usb_controller_error();
        /* This is a completed, non-cancelled connection failure, including
         * missing folders/unsupported filesystems. Preserve its evidence once
         * per attempt; healthy attachments and playback remain silent. */
        bool report_saved = false;
        int report_error = 0;
        exit_audit_enabled = true;
        char path[MAX_PATH_LEN];
        debug_failf("USB connection failed: stage=%s guard=%d io=%d", native_usb_ui_stage(),
                    window.guard_error, error);
        errno = 0;
        if (player_data_path("ndvideo-error.log.tns", path, sizeof(path)))
            report_saved = debug_dump_failure(path, NULL, "usb-connection");
        if (!report_saved) report_error = errno;
        if (report_saved) {
            FILE *file = fopen(path,"ab"); /* Complete the same failure report. */
            if (file) {
                native_usb_ui_debug(file);
                bool appended = !ferror(file);
                if (fclose(file) != 0) appended = false;
                if (!appended) { report_saved = false; report_error = errno; }
            } else {
                report_saved = false;
                report_error = errno;
            }
        }
        const char *message = window.guard_error
            ? "The USB connection could not be started." : usb_storage_error();
        UsbRegisters registers;
        uint32_t port = usb_registers_current(&registers) && usb_registers_valid(&registers)
            ? *(const volatile uint32_t *)(uintptr_t)registers.port : 0;
        char text[256], saved[80];
        if (report_saved) snprintf(saved, sizeof(saved), "Log saved in ndvideo-data.");
        else snprintf(saved, sizeof(saved), "Log could not be saved (%d).", report_error);
        snprintf(text, sizeof(text), "%.112s\nDiagnostic: %d/%d/%08lx\n%s",
                 message, window.guard_error, resume_result, (unsigned long)port, saved);
        queue_player_message("USB unavailable", text);
    }
    if (window.connected) {
        window.line1 = "USB drive ready.";
        window.line2 = "";
        window.status = NULL;
    } else if(window.audio_only) {
        window.line1="USB audio connected.";window.line2="";window.status=NULL;
    }
    /* The failure-report file writes above can disturb the timer again. */
    monotonic_clock_restore_player_timer();
    window.closing_mix=window.last_mix;
    window.closing = true;
    window.closed = monotonic_clock_now_ms();
    do {
        render_window(&window, true);
        if (!window.closing_mix || monotonic_clock_now_ms() - window.closed >= 160U)
            break;
        player_delay_ms(16);
    } while (1);
    pointer->press_edge = pointer->release_edge = false;
    usb_auto_connect_acknowledge(&auto_connection, connection_presence(), monotonic_clock_now_ms());
    display_power_note_activity(&g_display_power_state, monotonic_clock_now_ms());
    last_attachment_ready=usb_storage_attachment_ready();
    last_connection_ms=monotonic_clock_now_ms()-window.opened;
    last_connection_cancelled=window.cancelled;
    return window.connected && !window.cancelled;
}
bool usb_browser_connect(SDL_Surface *screen, const Fonts *fonts, PointerState *pointer,
                         MessageBackdropDraw backdrop, void *context)
{ return connect_storage(screen,fonts,pointer,backdrop,context,false); }
bool usb_browser_connect_automatic(SDL_Surface *screen, const Fonts *fonts, PointerState *pointer,
                                   MessageBackdropDraw backdrop, void *context)
{ return connect_storage(screen,fonts,pointer,backdrop,context,true); }
int usb_browser_retry(SDL_Surface *screen,const Fonts *fonts,PointerState *pointer,
                       LoadingProgress *loading)
{
    bool previous=quiet_reconnect;
    LoadingProgress *previous_progress=retry_progress;
    quiet_reconnect=true;
    retry_progress=loading;
    last_connection_cancelled=false;
    bool result=usb_browser_connect(screen,fonts,pointer,NULL,NULL);
    retry_progress=previous_progress;
    quiet_reconnect=previous;
    return last_connection_cancelled?-1:result?1:0;
}
typedef struct {
    int result;
} UsbShutdown;
void usb_browser_save_exit_audit(void)
{
    if (!connection_attempted || !exit_audit_enabled) return;
    unsigned mask = native_critical_enter();
    char path[MAX_PATH_LEN];
    /* Save the final handoff only for an explicitly requested USB capture
     * or an actual connection error. Normal successful exits stay quiet. */
    if (player_data_prepare() && player_data_path("ndvideo-usb-exit.log.tns", path, sizeof(path))) {
        FILE *file = player_error_log_open(path,true);
        if (file) {
            fprintf(file, "nMedia Player USB exit | %s %s rtc=%lu cpu_mask=%02x\n",
                    __DATE__, __TIME__, (unsigned long)connection_seconds(), mask);
            native_usb_ui_debug(file);
            usb_browser_debug(file);
            fclose(file);
        }
    }
    native_critical_leave(mask);
}
static void shutdown_native(void *context)
{
    unsigned mask = native_critical_enter();
    UsbShutdown *status = context;
    unsigned kernel_mask = 0;
    bool native_enabled = false;
    /* Finish our polled I/O with its own free-running timer. Native detach,
     * pipe cleanup and display restoration below need the OS timer instead. */
    while (!usb_storage_park()) { }
    if (native_usb_ui_active()) {
        const volatile uint32_t *counter;
        uint32_t hz;
        if (!native_usb_ui_clock(&counter, &hz, NULL, NULL) ||
            !monotonic_clock_enter_native(counter, hz)) {
            status->result = -23;
            goto done;
        }
        kernel_mask = TCT_Local_Control_Interrupts(0);
        native_critical_enter();
        native_enabled = true;
        /* Removal can be pending while the picker holds IRQs. Let that
         * native worker retire the attachment before closing its pipes. */
        if (!host_device_present()) {
            status->result = native_usb_ui_wait();
            native_critical_enter();
            if (status->result) goto done;
        }
    }
    bool stopped = usb_storage_shutdown();
    /* Native pipe cleanup and allocator calls may enable IRQs. Reestablish
     * the mapping snapshot's exclusion before restoring display ownership. */
    native_critical_enter();
    status->result = stopped ? native_usb_ui_release() : -21;
done:
    if (native_enabled) {
        native_critical_enter();
        TCT_Local_Control_Interrupts(kernel_mask);
        native_critical_enter();
    }
    monotonic_clock_leave_native();
    native_critical_leave(mask);
}
void usb_browser_shutdown(SDL_Surface *screen, const Fonts *fonts)
{
    if(!NDMEDIA_USB_STORAGE && !NDMEDIA_AUDIO) {
        player_standby_shutdown();
        return;
    }
    usb_fs_set_prepare(NULL,NULL);
    /* Both reservations can be active after a USB connection and standby.
     * Restore them in the reverse order they acquired the OS input/display. */
    if (!standby_was_outer)
        player_standby_shutdown();
    if (!usb_storage_memory_bytes() && !native_usb_ui_active()) {
        if (standby_was_outer)
            player_standby_shutdown();
        return;
    }
    raw_player_before_native();
    UsbShutdown status = {0};
    if (!sram_with_native_mapping(shutdown_native, &status))
        status.result = -20;
    bool previous = true;
    while (status.result) {
        SDL_FillRect(screen, NULL, 0);
        draw_ui_label(screen, fonts, 24, 84,
                      status.result == -21 ? "A USB callback is still finishing."
                                           : "Could not restore the OS display.");
        draw_ui_label(screen, fonts, 24, 104,
                      status.result == -21 ? "Esc retries USB cleanup."
                                           : "Esc retries display restoration.");
        present_screen(screen);
        bool down = isKeyPressed(KEY_NSPIRE_ESC);
        if (down && !previous)
            sram_with_native_mapping(shutdown_native, &status);
        previous = down;
        player_delay_ms(16);
    }
    if (standby_was_outer)
        player_standby_shutdown();
}
