#include "native_usb_ui_resolver.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned reads, page_checks;
static bool mapped = true;
static bool allow(void *context, uint32_t at, uint32_t bytes, unsigned kind)
{
    (void)context; (void)kind;
    if (bytes == 4096) ++page_checks;
    return mapped && at >= 0x10000000U && at <= 0x11000000U - bytes;
}
static bool read_word(void *context, uint32_t at, uint32_t *out)
{ (void)context; (void)at; ++reads; *out = 0; return true; }
static bool cancel(void *context)
{ ++*(unsigned *)context; return false; }
int main(void)
{
    PortableView view = {NULL, allow, read_word, 0x10000000U, 0x11000000U,
                         0x10000000U, 0x14000000U, NULL};
    NativeUsbUiApi api, other;
    unsigned calls = 0;
    int result;
    /* Unknown firmware must be scanned in bounded slices and never accepted. */
    do {
        reads = page_checks = 0;
        result = native_usb_ui_resolve_step(&view, false, &api, 1024);
        assert(reads <= 1024 && page_checks <= 2);
        assert(result != 1);
        ++calls;
    } while (!result && calls < 5000);
    assert(result == -1 && calls == 4097);
    reads = 0;
    assert(native_usb_ui_resolve_step(&view, false, &api, 1024) == -1 && !reads);
    /* A separate output starts fresh; cancellation preserves bounded progress. */
    assert(native_usb_ui_resolve_step(&view, false, &other, 1) == 0);
    unsigned cancelled = 0;
    reads = 0;
    assert(!native_usb_ui_resolve(&view, false, &other, cancel, &cancelled));
    assert(cancelled == 1 && !reads);
    assert(native_usb_ui_resolve_step(&view, false, &other, 1) == 0 && reads == 1);
    /* Changing families restarts rather than reusing the other model's result. */
    assert(native_usb_ui_resolve_step(&view, true, &other, 1) == 0 && other.classic_cx);
    mapped = false;
    page_checks = reads = 0;
    assert(native_usb_ui_resolve_step(&view, true, &other, 4) == 0);
    assert(page_checks == 4 && !reads);
    assert(native_usb_ui_resolve_step(&view, true, &other, 0) == -1);
    puts("PASS incremental firmware resolver: bounded mapped/unmapped scans, refusal, cancellation/resume and model isolation");
    return 0;
}
