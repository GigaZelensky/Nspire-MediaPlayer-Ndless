#include "usb_phase_timing.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#define NDMEDIA_USB_STORAGE 1
typedef int SDL_Surface;
enum { USB_AUTO_UNKNOWN=255, USB_AUTO_ADAPTER=1, PICKER_MOVIE, PICKER_FOLDER };
typedef struct {unsigned kind;bool timing_checked,folder_count_checked;} MovieFile;
static struct {MovieFile *files;size_t count;} g_picker_cache;
static struct { unsigned presence; } detection={USB_AUTO_UNKNOWN};
static int discovery_prepared, discovery_result, prepare_result;
static unsigned discovery_turns, preparations, wakes, parks, releases, invalidations;
static uint32_t discovery_after, now_ticks;
static bool mounted, pending, active, park_ok = true;
static bool quiescent=true;
static unsigned lease_releases;
static unsigned file_service_turns;
static bool usb_storage_mounted(void) { return mounted; }
static bool storage_native_active(void) { return active; }
static bool app_task_io_pending(void) { return pending; }
static void app_task_io_service(unsigned budget) {assert(budget==32U);++file_service_turns;}
static bool usb_storage_quiescent(void) { return quiescent; }
static void usb_browser_release_io(void) { ++lease_releases; }
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
    usb_browser_idle_discovery(&screen, 749);
    assert(wakes == 1);
    now_ticks = 750;
    usb_browser_idle_discovery(&screen, 750);
    assert(wakes == 2);
    now_ticks=1500;mounted=true;detection.presence=USB_AUTO_ADAPTER;
    MovieFile files[2]={{.kind=PICKER_MOVIE},{.kind=PICKER_FOLDER}};
    g_picker_cache.files=files;g_picker_cache.count=2;
    usb_browser_idle_discovery(&screen,1500);assert(wakes==2);
    files[0].timing_checked=true;
    usb_browser_idle_discovery(&screen,1500);assert(wakes==2);
    files[1].folder_count_checked=true;quiescent=false;
    usb_browser_idle_discovery(&screen,1500);assert(wakes==2);
    quiescent=true;usb_browser_idle_discovery(&screen,1500);
    assert(wakes==3 && lease_releases==3);
    discovery_prepared = -1;
    usb_browser_idle_discovery(&screen,2500);
    assert(preparations == 2 && wakes == 3);
    /* Audio recovery cannot wait for a root disconnect or picker metadata.
     * Native device cleanup behind a live mounted hub must still advance. */
    files[0].timing_checked=false;
    assert(usb_browser_recovery_poll(&screen) && wakes==4 && lease_releases==4);
    pending=true;assert(!usb_browser_recovery_poll(&screen) && wakes==4);pending=false;
    active=true;assert(!usb_browser_recovery_poll(&screen) && wakes==4);active=false;
    assert(file_service_turns==2);
    quiescent=false;assert(!usb_browser_recovery_poll(&screen) && wakes==4);quiescent=true;
    unsigned before_releases=releases;
    park_ok=false;assert(!usb_browser_recovery_poll(&screen) && wakes==4 && releases==before_releases+1);
    puts("PASS background discovery: one slice per frame, no blocking file joins, writer/reader exclusion and wake retry deadline");
    return 0;
}
