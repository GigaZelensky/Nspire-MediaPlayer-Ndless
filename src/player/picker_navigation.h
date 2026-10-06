#ifndef NDVIDEO_PICKER_NAVIGATION_H
#define NDVIDEO_PICKER_NAVIGATION_H
#include "player_internal.h"
enum { PICKER_NAV_NONE, PICKER_NAV_BACK, PICKER_NAV_USB };
int picker_navigation_poll(const PointerState *, bool, bool, bool, uint32_t, bool *);
void picker_navigation_reset(void);
void picker_navigation_draw(SDL_Surface *, const Fonts *, uint8_t, int);
void picker_navigation_animate(uint32_t, bool);
uint8_t picker_navigation_mix(uint32_t);
int picker_navigation_offset(uint32_t);
#endif
