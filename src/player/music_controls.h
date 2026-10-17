#ifndef NDVIDEO_MUSIC_CONTROLS_H
#define NDVIDEO_MUSIC_CONTROLS_H
#include <stdbool.h>
#include <stdint.h>
enum
{
    MUSIC_NONE,
    MUSIC_BACK,
    MUSIC_PREVIOUS,
    MUSIC_TOGGLE,
    MUSIC_NEXT,
    MUSIC_SEEK,
    MUSIC_MODE,
    MUSIC_VISUAL,
    MUSIC_INFO,
    MUSIC_ART,
    MUSIC_CONTROL_COUNT
};
enum
{
    MUSIC_SEEK_X = 16,
    MUSIC_SEEK_Y = 191,
    MUSIC_SEEK_W = 288,
    MUSIC_SEEK_H = 24
};
typedef struct
{
    uint32_t now, position, frames, rate;
    int x, y;
    bool duration_known, finished, pointer_visible, pressed, released, held;
    bool keyboard_pressed, keyboard_released, keyboard_held;
    bool back, toggle, next, previous, left_pressed, right_pressed, left_held, right_held;
    bool mode, visual, info, art_enabled;
} MusicControlInput;
typedef struct
{
    int capture, repeat_direction, drag_offset;
    uint32_t preview, repeat_at;
    uint32_t seek_at, badge_started, badge_hide_at;
    int32_t seek_delta_ms, badge_delta_ms;
    bool paused, scrubbing, pending_seek, info_open, art_open;
    bool capture_keyboard, capture_background;
    int capture_x,capture_y;
} MusicControls;
typedef struct
{
    int kind;
    uint32_t sample;
} MusicAction;
int music_control_at(int x, int y, bool duration_known);
bool music_hud_should_show(unsigned view,bool paused,bool modal,bool gesture,bool activity,
                           uint32_t now,uint32_t *until);
MusicAction music_controls_update(MusicControls *, const MusicControlInput *);
#endif
