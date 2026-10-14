#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
static bool resolved, refresh_ok = true;
static unsigned mask = 0x80, calls;
static int mapping, api, step_result;
static uint64_t ticks, tick_step;
static unsigned native_critical_enter(void) { unsigned old = mask; mask = 0xc0; return old; }
static void native_critical_leave(unsigned old) { mask = old; }
static bool portable_reader_platform_refresh(int *out) { assert(out == &mapping); return refresh_ok; }
static const int *portable_reader_platform_view(int *out) { return out; }
static unsigned nspire_hardware_id(void) { return 0x202; }
static bool nspire_asic_is_cx(unsigned id) { assert(id == 0x202); return false; }
static uint64_t monotonic_clock_now_ticks(void) { ticks += tick_step; return ticks; }
static int native_usb_ui_resolve_step(const int *view, bool cx, int *out, unsigned budget)
{ assert(view == &mapping && !cx && out == &api && budget == 256 && mask == 0xc0); ++calls; return step_result; }
/* PRODUCTION_PREPARE */
int main(void)
{
    assert(native_usb_ui_prepare_step() == 0 && calls == 128 && !resolved && mask == 0x80);
    calls = 0; tick_step = 131;
    assert(native_usb_ui_prepare_step() == 0 && calls == 1 && !resolved && mask == 0x80);
    calls = 0; step_result = -1;
    assert(native_usb_ui_prepare_step() == -1 && calls == 1 && !resolved && mask == 0x80);
    calls = 0; refresh_ok = false;
    assert(native_usb_ui_prepare_step() == -1 && !calls && !resolved && mask == 0x80);
    refresh_ok = true; step_result = 1;
    assert(native_usb_ui_prepare_step() == 1 && calls == 1 && resolved && mask == 0x80);
    calls = 0;
    assert(native_usb_ui_prepare_step() == 1 && !calls && mask == 0x80);
    puts("PASS UI preparation: tick deadline, stopped-clock bound, partial/refused bindings unpublished and IRQ policy restored");
    return 0;
}
