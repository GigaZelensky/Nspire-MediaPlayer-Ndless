#ifndef NDVIDEO_USB_FS_WORKER_H
#define NDVIDEO_USB_FS_WORKER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {
    void (*run)(void *);
    void *argument;
    bool pending, done, cancelled;
    int error;
} UsbFsJob;
typedef struct {
    void *foreground, *background;
} UsbFsContext;
void usb_fs_context_init(UsbFsContext *, void *, size_t, void (*)(void *), void *);
void usb_fs_context_resume(UsbFsContext *);
void usb_fs_context_yield(UsbFsContext *);
void usb_fs_context_destroy(UsbFsContext *);
bool usb_fs_submit(UsbFsJob *);
/* Allocate the worker at a safe native-OS boundary, before polled I/O. */
bool usb_fs_prepare(void);
void usb_fs_debug(FILE *);
void usb_fs_service(uint32_t);
/* Foreground only, before entering the worker: 1 ready, 0 defer, -1 cancel.
 * The hook may prepare native host ownership; it must not resume this worker. */
void usb_fs_set_prepare(int (*)(void *),void *);
void usb_fs_cancel(UsbFsJob *);
void usb_fs_cancel_active(void);
void usb_fs_wait(UsbFsJob *);
bool usb_fs_idle(void);
size_t usb_fs_memory_bytes(void);
void usb_fs_shutdown(void);
void usb_fs_yield(void);
bool usb_fs_cancelled(void);
/* FatFs CPU traversal hook. No native calls, allocation, or scheduling. */
bool ndvideo_usb_fs_poll(void);
void usb_fs_set_progress(void (*)(void *), void *);
#endif
