#ifndef NDVIDEO_NATIVE_USB_UI_RESOLVER_H
#define NDVIDEO_NATIVE_USB_UI_RESOLVER_H
#include "../platform/portable_resolver.h"
enum {
    UI_DISPLAY_CONTROL,
    UI_DISPLAY_START,
    UI_DISPLAY_STOP,
    UI_DISPLAY_HISR_CREATE,
    UI_BUSY_STOP,
    UI_BUSY_START,
    UI_CURSOR_HIDE,
    UI_CURSOR_SHOW,
    UI_APD_ALLOW,
    UI_FUNCTIONS
};
typedef struct {
    uint32_t function[UI_FUNCTIONS];
    uint32_t display_descriptor, display_state, display_timer, display_hisr;
    uint32_t busy_base, busy_callback, busy_interval;
    uint32_t cursor_count, cursor_device_hide, event_mask, tracking_flags, dim_disable;
} NativeUsbUiApi;
bool native_usb_ui_resolve(const PortableView *, NativeUsbUiApi *, bool (*)(void *), void *);
#endif
