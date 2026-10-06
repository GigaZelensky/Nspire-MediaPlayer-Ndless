#include "usb_browser.h"
#include "native_usb_ui.h"
#include "native_interrupts.h"
#include "performance_clock.h"
#include "raw_player_io.h"
#include "../storage/usb/usb_storage.h"
#include "../storage/usb/usb_controller.h"
#include "../storage/usb/usb_fs_worker.h"
#include "../sram.h"
#include "media_browser.h"
#include <errno.h>

extern bool player_standby_guard_active(void);
static bool standby_was_outer;

typedef struct {
    SDL_Surface *screen;
    const Fonts *fonts;
    PointerState *pointer;
    MessageBackdropDraw backdrop;
    void *backdrop_context;
    UiTransition hover, press;
    uint32_t opened, closed, last_draw;
    bool closing, cancelled, connected, previous_enter, previous_escape, armed, armed_pointer;
    bool timed_out;
    const char *line1, *line2, *status;
    int guard_error;
} UsbConnectWindow;
static void native_wait_frame(void)
{
    /* Ndless msleep masks the very USB interrupt being serviced here. Use the
     * already configured free-running timer and preserve the caller's mask. */
    unsigned mask = native_interrupt_mask();
    uint32_t start = usb_controller_ticks();
    native_critical_leave(0);
    for (unsigned polls = 0; polls < 2000000U; ++polls)
        if ((uint32_t)(usb_controller_ticks() - start) >= 524U)
            break;
    native_critical_leave(mask);
}
static void render_window(UsbConnectWindow *window, bool force)
{
    uint32_t now = monotonic_clock_now_ms();
    if (!force && window->last_draw && now - window->last_draw < 16U)
        return;
    window->last_draw = now;
    PointerState *pointer = window->pointer;
    pointer_update(pointer);
    bool enter = isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_RET) ||
                 isKeyPressed(KEY_NSPIRE_5);
    bool escape = isKeyPressed(KEY_NSPIRE_ESC);
    if (!window->closing) {
        if ((enter && !window->previous_enter) || (escape && !window->previous_escape))
            window->armed = true;
        if (pointer->press_edge)
            window->armed_pointer = true;
        if ((window->armed && !enter && !escape) || (window->armed_pointer && !pointer->down)) {
            window->cancelled = true;
            usb_fs_cancel_active();
            ui_transition_begin_press_release(&window->press, now);
        }
    }
    window->previous_enter = enter;
    window->previous_escape = escape;
    uint8_t mix = window->closing ? 255U - ui_ease_out_cubic(now - window->closed, 160)
                                  : ui_ease_out_cubic(now - window->opened, 180);
    SDL_Rect panel = {30, 60 + (255 - mix) * 12 / 255, 260, 128},
             button = {124, panel.y + 94, 72, 23};
    bool hot = pointer->visible && rect_contains_point(&button, pointer->x, pointer->y);
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
        const char *lines[] = {"USB drive", window->line1, window->line2, window->status};
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
    render_window(context, false);
}
static bool prepare_progress(void *context)
{
    progress(context);
    return !((UsbConnectWindow *)context)->cancelled;
}
static void connect_native(void *context)
{
    UsbConnectWindow *window = context;
    unsigned saved = native_critical_enter();
    unsigned kernel_mask = 0;
    bool native_enabled = false;
    if (!native_usb_ui_active())
        standby_was_outer = player_standby_guard_active();
    window->guard_error = native_usb_ui_acquire(prepare_progress, window);
    if (window->guard_error) {
        debug_tracef("USB UI refused stage=%s error=%d", native_usb_ui_stage(),
                     window->guard_error);
        native_usb_ui_release();
        goto done;
    }
    if (window->cancelled)
        goto done;
    if (!usb_storage_prepare_connection())
        goto done;
    /* The native USB worker needs Nucleus's interrupt policy enabled too,
     * not just a temporary clear of the CPU IRQ bit. Restore both on return. */
    kernel_mask = TCT_Local_Control_Interrupts(0);
    native_critical_enter();
    native_enabled = true;
    window->line1 = "Connect the drive to the OTG adapter.";
    window->line2 = "Keep the adapter in the calculator.";
    window->status = "Waiting for a USB drive...";
    uint32_t detected = 0, entered = monotonic_clock_now_ms();
    unsigned registrations = 1;
    while (!window->cancelled) {
        native_critical_enter();
        if (!usb_storage_driver_registered()) {
            if (registrations++ >= 3 || !usb_storage_prepare_connection())
                break;
            window->line1 = "Reconnect the drive to the adapter.";
            detected = 0;
        }
        uint32_t now = monotonic_clock_now_ms();
        if (now - entered >= 30000U) {
            window->timed_out = true;
            usb_controller_set_error(
                "No USB drive detected. Reconnect the drive to the OTG adapter.", ENODEV);
            break;
        }
        if (usb_storage_attachment_ready()) {
            if (!detected)
                detected = now;
            window->status = "Opening the USB drive...";
            if (now - detected >= 250U) {
                render_window(window, true);
                if (usb_storage_finish_connection()) {
                    int error = 0;
                    window->status = "Reading folders...";
                    render_window(window, true);
                    usb_fs_set_progress(progress, window);
                    window->connected = usb_volume_mount(&error);
                    usb_fs_set_progress(NULL, NULL);
                }
                break;
            }
        } else {
            detected = 0;
            if (now - entered >= 3000U)
                window->line1 = "Reconnect the drive to the adapter.";
        }
        render_window(window, true);
        native_wait_frame();
        window->guard_error = native_usb_ui_reassert();
        if (window->guard_error)
            break;
    }
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
    if (native_enabled) {
        native_critical_enter();
        TCT_Local_Control_Interrupts(kernel_mask);
    }
    native_critical_leave(saved);
}
bool usb_browser_connect(SDL_Surface *screen, const Fonts *fonts, PointerState *pointer,
                         MessageBackdropDraw backdrop, void *context)
{
    raw_player_before_native();
    UsbConnectWindow window = {.screen = screen,
                               .fonts = fonts,
                               .pointer = pointer,
                               .backdrop = backdrop,
                               .backdrop_context = context,
                               .line1 = "Preparing the USB connection.",
                               .line2 = "",
                               .status = "Checking the display interface...",
                               .opened = monotonic_clock_now_ms(),
                               .previous_enter = isKeyPressed(KEY_NSPIRE_ENTER),
                               .previous_escape = isKeyPressed(KEY_NSPIRE_ESC)};
    while (monotonic_clock_now_ms() - window.opened < 180U) {
        render_window(&window, true);
        player_delay_ms(16);
    }
    if (!sram_with_native_mapping(connect_native, &window))
        window.guard_error = -20;
    if (!window.cancelled && !window.connected) {
        int error = usb_controller_error();
        bool report =
            window.guard_error || window.timed_out ||
            (error && error != ENODEV && error != ENOENT && error != ENOTSUP && error != ENOMEM);
        if (report) {
            char path[MAX_PATH_LEN];
            snprintf(path, sizeof(path), "%s/ndvideo-error.log.tns", media_browser_root());
            debug_failf("USB connection failed: stage=%s guard=%d io=%d", native_usb_ui_stage(),
                        window.guard_error, error);
            if (debug_dump_failure(path, NULL, "usb-connection")) {
                FILE *file = fopen(path, "ab");
                if (file) {
                    native_usb_ui_debug(file);
                    usb_storage_debug(file);
                    fclose(file);
                }
            }
        }
        queue_player_message("USB unavailable",
                             window.guard_error
                                 ? "The OS USB display interface could not be reserved."
                                 : usb_storage_error());
    }
    window.closing = true;
    window.closed = monotonic_clock_now_ms();
    do {
        render_window(&window, true);
        if (monotonic_clock_now_ms() - window.closed >= 160U)
            break;
        player_delay_ms(16);
    } while (1);
    pointer->press_edge = pointer->release_edge = false;
    display_power_note_activity(&g_display_power_state, monotonic_clock_now_ms());
    return window.connected && !window.cancelled;
}
typedef struct {
    int result;
} UsbShutdown;
static void shutdown_native(void *context)
{
    unsigned mask = native_critical_enter();
    ((UsbShutdown *)context)->result = usb_storage_shutdown() ? native_usb_ui_release() : -21;
    native_critical_leave(mask);
}
void usb_browser_shutdown(SDL_Surface *screen, const Fonts *fonts)
{
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
