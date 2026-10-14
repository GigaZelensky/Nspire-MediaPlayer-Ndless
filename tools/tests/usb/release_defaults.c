#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MAX_PATH_LEN 128
#define KEY_NSPIRE_D 1
#define NDVIDEO_CX_SRAM_SURVEY 0
#define NDVIDEO_CX_SRAM_EXPERIMENTAL 0
typedef struct { bool checkpoint_enabled, checkpoint_started; } UsbConnectWindow;
static bool verbose, d_down, connection_attempted, exit_audit_enabled;
static unsigned mask, prepare_calls, critical_calls;
static const char *connection_stage;
static bool debug_is_runtime_logging_enabled(void) { return verbose; }
static bool isKeyPressed(int key) { return key == KEY_NSPIRE_D && d_down; }
static bool logging_requested(void) { return /* LOGGING_POLICY */; }
static unsigned native_critical_enter(void)
{ ++critical_calls; unsigned old = mask; mask = 0xc0; return old; }
static void native_critical_leave(unsigned old) { mask = old; }
static bool player_data_prepare(void) { ++prepare_calls; return true; }
static bool player_data_path(const char *name, char *out, size_t count)
{ (void)name; snprintf(out, count, "unused-test-path"); return true; }
static uint32_t monotonic_clock_now_ms(void) { return 0; }
static uint32_t connection_seconds(void) { return 0; }
static void usb_storage_debug(FILE *file) { (void)file; }
static void native_usb_ui_debug(FILE *file) { (void)file; }
static void monotonic_clock_debug(FILE *file) { (void)file; }
static void native_writer_debug(FILE *file) { (void)file; }
static void usb_browser_debug(FILE *file) { (void)file; }
static FILE *diagnostic_open(const char *name, const char *mode)
{ (void)name; return strcmp(mode, "rb") ? tmpfile() : NULL; }
#define fopen diagnostic_open
/* PRODUCTION_CHECKPOINT */
/* PRODUCTION_EXIT_AUDIT */
static uint32_t model;
static unsigned sram_impl_calls, survey_calls;
static const char *sram_status;
static uint32_t nspire_hardware_id(void) { return model; }
static bool nspire_asic_is_cx(uint32_t id) { return id == 0x101 || id == 0x10000101; }
static void sram_survey_checkpoint(const char *phase) { (void)phase; ++survey_calls; }
static void sram_set_status_message(const char *message) { sram_status = message; }
static unsigned sram_critical_enter(void) { return native_critical_enter(); }
static void sram_critical_leave(unsigned old) { native_critical_leave(old); }
static bool sram_init_impl(void) { ++sram_impl_calls; return true; }
/* PRODUCTION_SRAM_INIT */
int main(void)
{
    UsbConnectWindow window = {0};
    assert(!logging_requested());
    connection_checkpoint(&window, "connected", true);
    connection_attempted = true;
    usb_browser_save_exit_audit();
    assert(!prepare_calls && !critical_calls && !window.checkpoint_started);
    assert(!strcmp(connection_stage, "connected"));
    d_down = true;
    assert(logging_requested()); d_down = false;
    verbose = true;
    assert(logging_requested()); verbose = false;
    window.checkpoint_enabled = true;
    connection_checkpoint(&window, "requested capture", true);
    assert(window.checkpoint_started && prepare_calls == 1 && !mask);
    exit_audit_enabled = true; /* Explicit diagnostics or an actual error. */
    usb_browser_save_exit_audit();
    assert(prepare_calls == 2 && !mask);
    for (unsigned alias = 0; alias < 2; ++alias) {
        model = alias ? 0x10000101 : 0x101;
        unsigned before = critical_calls;
        assert(!sram_init() && !sram_impl_calls && critical_calls == before);
        assert(!strcmp(sram_status, "disabled on CX; using RAM"));
    }
    model = 0x202;
    assert(sram_init() && sram_impl_calls == 1 && survey_calls == 3 && !mask);
    puts("PASS quiet USB defaults, explicit/error audit gates, original-CX RAM fallback and retained CX II SRAM path");
    return 0;
}
