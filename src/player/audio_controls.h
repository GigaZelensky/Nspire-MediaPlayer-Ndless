#ifndef NDVIDEO_AUDIO_CONTROLS_H
#define NDVIDEO_AUDIO_CONTROLS_H
#include <stdbool.h>
#include <stdint.h>
struct AudioPlayback;
typedef struct {
    uint32_t now_ms;
    int x, y;
    bool pointer_visible, pressed, released, held, keyboard_pressed;
    bool up_pressed, down_pressed, up_held, down_held, mute_pressed;
    bool chrome_visible, enabled;
} AudioControlInput;
enum { AUDIO_KEYS_NONE, AUDIO_KEYS_VOLUME, AUDIO_KEYS_BRIGHTNESS };
static inline unsigned audio_vertical_keys(bool ctrl, bool shift)
{ return ctrl ? AUDIO_KEYS_NONE : shift ? AUDIO_KEYS_BRIGHTNESS : AUDIO_KEYS_VOLUME; }
void audio_controls_reset(void);
void audio_controls_set_vertical_offset(int);
/* True consumes the entire pointer gesture, including release outside. */
bool audio_controls_update(struct AudioPlayback *, const AudioControlInput *);
bool audio_controls_open(void);
bool audio_controls_touchpad_captured(void);
bool audio_controls_keyboard_captured(void);
bool audio_controls_animating(void);
uint32_t audio_controls_revision(void);
#endif
