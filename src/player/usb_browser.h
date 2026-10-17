#ifndef NDVIDEO_USB_BROWSER_H
#define NDVIDEO_USB_BROWSER_H
#include "player_internal.h"
bool usb_browser_connect(SDL_Surface *, const Fonts *, PointerState *,
                         void (*)(SDL_Surface *, void *, uint32_t), void *);
int usb_browser_retry(SDL_Surface *, const Fonts *, PointerState *, LoadingProgress *);
bool usb_browser_device_present(void);
bool usb_browser_adapter_present(void);
void usb_browser_note_disconnect(void);
bool usb_browser_auto_pending(uint32_t now);
void usb_browser_idle_discovery(SDL_Surface *screen, uint32_t now);
/* Failed audio must be suspended first. Service native USB workers even when
 * the upstream hub is present and the picker/storage scan is already done. */
bool usb_browser_recovery_poll(SDL_Surface *screen);
void usb_browser_request_session_restart(void);
void usb_browser_debug(FILE *file);
void usb_browser_save_exit_audit(void);
void usb_browser_shutdown(SDL_Surface *, const Fonts *);
void usb_browser_reserve_native_ui_order(void);
/* Retire app bus ownership before foreground OS work. Pending bulk commands
 * reach a proven boundary first; the filesystem reacquires before resuming. */
void usb_browser_release_io(void);
#endif
