#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
static unsigned mask = 0x80, policy = 7, test_case, waits, restores;
static bool native_time, standby_was_outer;
static int discovery_result;
static uint32_t counter;
static unsigned native_critical_enter(void) { unsigned old = mask; mask = 0xc0; return old; }
static void native_critical_leave(unsigned old) { mask = old; }
static bool discovery_progress(void *context) { (void)context; return true; }
static bool native_usb_ui_clock(const volatile uint32_t **out, uint32_t *hz,
                                bool (*progress)(void *), void *context)
{ (void)progress; (void)context; *out = &counter; *hz = 50; return test_case != 1; }
static bool monotonic_clock_enter_native(const volatile uint32_t *address, uint32_t hz)
{ assert(address == &counter && hz == 50 && !native_time); native_time = test_case != 2; return native_time; }
static unsigned TCT_Local_Control_Interrupts(unsigned requested)
{ unsigned old = policy; policy = requested; return old; }
static bool native_usb_ui_active(void) { return false; }
static bool player_standby_guard_active(void) { return true; }
static int native_usb_ui_acquire(bool (*progress)(void *), void *context)
{ (void)progress; (void)context; assert(native_time && policy == 0); return test_case == 3 ? -3 : 0; }
static int native_usb_ui_wait(void)
{ ++waits; assert(native_time && policy == 0 && mask == 0xc0); return test_case == 4 ? -4 : 0; }
static int native_usb_ui_reassert(void) { return test_case == 5 ? -5 : 0; }
static void usb_storage_watch_attachments(void) { assert(native_time && policy==0); }
static bool performance_clock_start(void) { assert(native_time && policy==7);return true; }
static void monotonic_clock_leave_native(void) { assert(native_time); native_time = false; }
static bool monotonic_clock_restore_player_timer(void)
{ assert(!native_time && policy == 7); ++restores; return true; }
/* PRODUCTION_DISCOVERY */
int main(void)
{
    for (test_case = 0; test_case <= 5; ++test_case) {
        waits = restores = 0;
        discovery_native(NULL);
        assert(mask == 0x80 && policy == 7 && !native_time && restores == 1);
        assert(waits == (test_case == 0 || test_case >= 4));
        assert(test_case ? discovery_result != 0 : discovery_result == 0);
    }
    assert(standby_was_outer);
    puts("PASS idle discovery wake: one native tick, timer/policy/mask restoration and refused/failed-stage cleanup");
    return 0;
}
