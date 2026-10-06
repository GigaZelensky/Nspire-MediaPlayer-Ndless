#ifndef NDVIDEO_USB_BROWSER_H
#define NDVIDEO_USB_BROWSER_H
#include "player_internal.h"
bool usb_browser_connect(SDL_Surface *, const Fonts *, PointerState *,
                         void (*)(SDL_Surface *, void *, uint32_t), void *);
void usb_browser_shutdown(SDL_Surface *, const Fonts *);
#endif
