#ifndef NMEDIA_WALLPAPER_IMAGE_H
#define NMEDIA_WALLPAPER_IMAGE_H
#include <stdint.h>
/* Recolor only chromatic material. Glass whites and neutral reflections stay
 * neutral. The original Amber palette is an exact identity. */
uint16_t wallpaper_recolor(uint16_t pixel,uint16_t accent);
uint16_t wallpaper_darken(uint16_t pixel);
uint16_t wallpaper_saturate(uint16_t pixel);
const uint16_t *wallpaper_image(uint16_t accent);
int wallpaper_cached(void);
/* Foreground-only cooperative service, used while music owns USB audio.
 * Invoked between bounded decode/recolor/drawing slices, never from an IRQ. */
void wallpaper_set_service(void (*service)(void *),void *context);
void wallpaper_service(void);
const uint16_t *wallpaper_previous(void);
void wallpaper_finish_transition(void);
void wallpaper_release(void);
#endif
