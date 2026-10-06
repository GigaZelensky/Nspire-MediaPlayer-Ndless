#include "native_usb_ui.h"
#include "native_usb_ui_resolver.h"
#include "native_interrupts.h"
#include "../platform/portable_reader_platform.h"
#include <limits.h>

#define WORD(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define HALF(a) (*(volatile uint16_t *)(uintptr_t)(a))
static NativeUsbUiApi api;
static PortableReaderPlatform mapping;
static bool resolved, input_held, apd_held, busy_held, display_held, cursor_held;
static uint32_t original_input, original_tracking, original_display, original_busy, original_cursor;
static const char *stage = "idle";
const char *native_usb_ui_stage(void)
{
    return stage;
}
void native_usb_ui_debug(FILE *file)
{
    if (!file)
        return;
    fprintf(file, "usb_ui stage=%s resolved=%u input=%u apd=%u busy=%u display=%u cursor=%u\n",
            stage, resolved, input_held, apd_held, busy_held, display_held, cursor_held);
    for (unsigned i = 0; i < UI_FUNCTIONS; ++i)
        fprintf(file, "usb_ui_function_%u=%08lx\n", i, (unsigned long)api.function[i]);
    fprintf(file,
            "usb_ui_display=%08lx state=%08lx busy=%08lx cursor_count=%08lx event_mask=%08lx "
            "tracking=%08lx\n",
            (unsigned long)api.display_descriptor, (unsigned long)api.display_state,
            (unsigned long)api.busy_base, (unsigned long)api.cursor_count,
            (unsigned long)api.event_mask, (unsigned long)api.tracking_flags);
}
bool native_usb_ui_active(void)
{
    return input_held || apd_held || busy_held || display_held || cursor_held;
}
static void call(unsigned role)
{
    ((void (*)(void))(uintptr_t)api.function[role])();
    native_critical_enter();
}
static void boolean_call(uint32_t function, unsigned value)
{
    ((void (*)(unsigned))(uintptr_t)function)(value);
    native_critical_enter();
}
static bool memory(uint32_t at, unsigned bytes)
{
    const PortableView *v = portable_reader_platform_view(&mapping);
    return v && !(at & 3) && v->allow_span(v->context, at, bytes, PORTABLE_DATA);
}
static bool timer_valid(uint32_t timer)
{
    return memory(timer, 96) && WORD(timer + 12) == 0x54494d45U &&
           WORD(timer + 24) == api.busy_callback && WORD(timer + 28) == 0 &&
           WORD(timer + 68) == 0 && (WORD(timer + 32) & 255) <= 1 && WORD(timer + 80) <= 1;
}
static bool timer_running(uint32_t timer)
{
    return timer && timer_valid(timer) && (WORD(timer + 32) & 255) == 1 && WORD(timer + 80) == 1;
}
static bool display_disabled(void)
{
    uint32_t s = api.display_state;
    return !WORD(s + 0x58) && !HALF(s + 0x54) && !WORD(s + 0x1c) && !WORD(s + 0x90);
}
static bool display_enabled(void)
{
    uint32_t s = api.display_state;
    return WORD(s + 0x58) == 1 && WORD(s + 0x1c) == 0x54494d45U && (WORD(s + 0x30) & 255) == 1 &&
           WORD(s + 0x28) == api.display_timer && WORD(s + 0x90) == 0x48495352U &&
           WORD(s + 0xc8) == api.display_hisr;
}
static void display_control(unsigned enabled)
{
    /* Its stop routine waits for LCD DMA completion, serviced by the OS IRQ. */
    native_critical_leave(0);
    ((int (*)(unsigned))(uintptr_t)api.function[UI_DISPLAY_CONTROL])(enabled);
    native_critical_enter();
    portable_reader_platform_refresh(&mapping);
}
int native_usb_ui_acquire(bool (*progress)(void *), void *context)
{
    if (native_interrupt_mask() != 0xc0U)
        return -1;
    if (native_usb_ui_active()) {
        if (input_held && apd_held && busy_held && display_held && cursor_held)
            return native_usb_ui_reassert();
        if (native_usb_ui_release())
            return -19;
    }
    stage = "mapping";
    if (!portable_reader_platform_refresh(&mapping))
        return -2;
    if (!resolved) {
        stage = "OS display interface";
        if (!native_usb_ui_resolve(portable_reader_platform_view(&mapping), &api, progress,
                                   context))
            return -3;
        resolved = true;
    }
    stage = "display state";
    if (!memory(api.display_state, 0xdc + 2048) || WORD(api.display_state) != 1 ||
        WORD(api.display_state + 12) != 153600 || (!display_disabled() && !display_enabled()))
        return -4;
    stage = "cursor state";
    int count = (int16_t)HALF(api.cursor_count);
    uint32_t timer = WORD(api.busy_base + 4);
    if (count > 0 || count <= INT16_MIN || (timer && !timer_valid(timer)) ||
        WORD(api.busy_base + 12) > 1)
        return -5;
    original_busy = timer_running(timer);
    if (original_busy && (WORD(api.busy_interval) < 10 || WORD(api.busy_interval) > INT32_MAX))
        return -6;
    original_input = HALF(api.event_mask) & 31U;
    original_tracking = HALF(api.tracking_flags) & 8U;
    original_cursor = WORD(0xc0000c00U);
    original_display = WORD(api.display_state + 0x58);
    HALF(api.event_mask) &= (uint16_t)~31U;
    HALF(api.tracking_flags) &= (uint16_t)~8U;
    input_held = true;
    stage = "automatic power";
    boolean_call(api.dim_disable, 0);
    boolean_call(api.function[UI_APD_ALLOW], 0);
    apd_held = true;
    stage = "busy cursor";
    busy_held = true;
    if (timer || WORD(api.busy_base + 12))
        call(UI_BUSY_STOP);
    WORD(0xc0000c00U) = original_cursor & ~1U;
    if (WORD(api.busy_base + 4) || WORD(api.busy_base + 12))
        return -7;
    stage = "pointer cursor";
    cursor_held = true;
    call(UI_CURSOR_HIDE);
    ((int (*)(void))(uintptr_t)api.cursor_device_hide)();
    native_critical_enter();
    if ((int16_t)HALF(api.cursor_count) >= 0 || (WORD(0xc0000c00U) & 1))
        return -8;
    stage = "display copier";
    display_held = true;
    if (original_display)
        display_control(0);
    if (!display_disabled())
        return -9;
    stage = "ready";
    return 0;
}
int native_usb_ui_reassert(void)
{
    if (!portable_reader_platform_refresh(&mapping) || !native_usb_ui_active())
        return -10;
    if (input_held) {
        HALF(api.event_mask) &= (uint16_t)~31U;
        HALF(api.tracking_flags) &= (uint16_t)~8U;
    }
    if (busy_held) {
        uint32_t timer = WORD(api.busy_base + 4);
        if (timer && !timer_valid(timer))
            return -11;
        if (timer || WORD(api.busy_base + 12))
            call(UI_BUSY_STOP);
    }
    if (display_held && !display_disabled()) {
        if (!display_enabled())
            return -12;
        display_control(0);
        if (!display_disabled())
            return -13;
    }
    if (cursor_held) {
        ((int (*)(void))(uintptr_t)api.cursor_device_hide)();
        native_critical_enter();
    }
    WORD(0xc0000c00U) &= ~1U;
    return 0;
}
int native_usb_ui_release(void)
{
    if (!native_usb_ui_active())
        return 0;
    if (!portable_reader_platform_refresh(&mapping))
        return -14;
    stage = "restore display";
    if (display_held) {
        if (original_display && display_disabled())
            display_control(1);
        if (original_display ? !display_enabled() : !display_disabled())
            return -15;
        display_held = false;
    }
    stage = "restore cursor";
    if (cursor_held) {
        call(UI_CURSOR_SHOW);
        cursor_held = false;
    }
    if (busy_held) {
        uint32_t timer = WORD(api.busy_base + 4);
        if (timer && !timer_valid(timer))
            return -16;
        if (original_busy) {
            if (!timer)
                call(UI_BUSY_START);
            if (!timer_running(WORD(api.busy_base + 4)))
                return -17;
        } else if (timer) {
            call(UI_BUSY_STOP);
            if (WORD(api.busy_base + 4))
                return -18;
        }
        WORD(0xc0000c00U) = original_cursor;
        busy_held = false;
    }
    if (apd_held) {
        boolean_call(api.function[UI_APD_ALLOW], 1);
        apd_held = false;
    }
    if (input_held) {
        HALF(api.event_mask) = (HALF(api.event_mask) & (uint16_t)~31U) | original_input;
        HALF(api.tracking_flags) = (HALF(api.tracking_flags) & (uint16_t)~8U) | original_tracking;
        input_held = false;
    }
    stage = "idle";
    return 0;
}
