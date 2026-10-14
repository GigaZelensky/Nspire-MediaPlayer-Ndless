#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef int SDL_Surface;
enum { USB_AUTO_UNKNOWN, USB_AUTO_PRESENT };
static struct { unsigned presence; } detection;
static int discovery_prepared, discovery_result, prepare_result;
static unsigned discovery_turns, preparations, wakes, parks, releases, invalidations;
static uint32_t discovery_after, now_ticks;
static bool mounted, pending, active, park_ok = true;
static bool usb_storage_mounted(void) { return mounted; }
static bool storage_native_active(void) { return active; }
static bool app_task_io_pending(void) { return pending; }
static int native_usb_ui_prepare_step(void) { ++preparations; return prepare_result; }
static bool raw_player_prepare_native(void) { ++parks; return park_ok; }
static void raw_player_native_released(void) { ++releases; }
static void storage_mutation_invalidate(void) { ++invalidations; }
static void discovery_native(void *context) { assert(context && !pending && !active); ++wakes; }
static bool sram_with_native_mapping(void (*operation)(void *), void *context)
{ operation(context); return true; }
static uint32_t monotonic_clock_now_ms(void) { return now_ticks; }
static unsigned connection_presence(void) { return USB_AUTO_UNKNOWN; }
/* PRODUCTION_IDLE */
int main(void)
{
    SDL_Surface screen = 0;
    /* Initial partial scan returns to the picker without servicing file work. */
    usb_browser_idle_discovery(&screen, 0);
    assert(preparations == 1 && !wakes && !parks && !releases);
    active = true;
    usb_browser_idle_discovery(&screen, 0);
    assert(preparations == 1);
    active = false; prepare_result = 1;
    usb_browser_idle_discovery(&screen, 0);
    assert(preparations == 2 && discovery_prepared == 1 && !wakes);
    pending = true;
    usb_browser_idle_discovery(&screen, 0);
    assert(!wakes && !parks);
    pending = false; park_ok = false;
    usb_browser_idle_discovery(&screen, 0);
    assert(parks == 1 && releases == 1 && !wakes && !invalidations);
    park_ok = true;
    usb_browser_idle_discovery(&screen, 0);
    assert(parks == 2 && releases == 2 && wakes == 1 && invalidations == 1);
    usb_browser_idle_discovery(&screen, 499);
    assert(wakes == 1);
    now_ticks = 500;
    usb_browser_idle_discovery(&screen, 500);
    assert(wakes == 2);
    discovery_prepared = -1;
    usb_browser_idle_discovery(&screen, 1000);
    assert(preparations == 2 && wakes == 2);
    puts("PASS background discovery: one slice per frame, no blocking file joins, writer/reader exclusion and wake retry deadline");
    return 0;
}
