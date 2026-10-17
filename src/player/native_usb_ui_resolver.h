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
    UI_TASK_SLEEP,
    UI_CLOCK_TIME,
    UI_HOST_LAYOUT,
    UI_BUS_LIST,
    UI_OTG_ATTACH,
    UI_HCD_CONTROL,
    UI_HUB_CONTROL,
    UI_BSD_LOCK,
    UI_BSD_TAKE,
    UI_BSD_GIVE,
    UI_SEM_TAKE,
    UI_SEM_GIVE,
    UI_HCD_RESUME,
    UI_BUS_RESUME,
    UI_HUB_RESUME,
    UI_EHCI_WAKE,
    UI_PROBE_DEVICE,
    UI_PROBE_BIND,
    UI_BUS_WAKE,
    UI_NEW_DEVICE,
    UI_BUSY_CALLBACK,
    UI_DIM_DISABLE,
    UI_CX_FEATURES,
    UI_FUNCTIONS,
    /* Checked through the live HCD's call graph, not global scan slots. */
    UI_HCD_CORE_CONTROL,
    UI_EHCI_START,
    UI_IRQ_REGISTER,
    UI_HUB_EXPLORE,
    UI_PORT_RESET,
    UI_COMPOSITE_ATTACH,
    UI_OTG_ENGINE_CREATE,
    UI_OTG_APP_MODE,
    UI_OTG_APP_NOTIFY,
    UI_OTG_SESSION_REQUEST,
    UI_OTG_SESSION_END,
    UI_OTG_A_IDLE,
    UI_OTG_WAIT_BCON,
    UI_OTG_WAIT_VFALL,
    UI_OTG_INPUT_POLL
};
typedef struct {
    uint32_t function[UI_FUNCTIONS];
    uint32_t display_descriptor, display_state, display_timer, display_hisr;
    uint32_t busy_base, busy_callback, busy_interval;
    uint32_t cursor_count, cursor_device_hide, event_mask, tracking_flags, dim_disable;
    uint32_t task_global;
    uint32_t clock_counter, clock_hz, host_global, bus_node;
    uint32_t bsd_locks, bsd_mode, direct_lcd_flags;
    bool classic_cx;
} NativeUsbUiApi;
bool native_usb_ui_role_matches(const PortableView *, uint32_t address, unsigned role);
/* 0 pending, 1 fully validated, -1 refused. Bounds the scan words per call;
 * partial bindings must not be called. Resumes the same API/model scan. */
int native_usb_ui_resolve_step(const PortableView *, bool classic_cx, NativeUsbUiApi *, unsigned word_budget);
bool native_usb_ui_resolve(const PortableView *, bool classic_cx, NativeUsbUiApi *, bool (*)(void *), void *);
#endif
