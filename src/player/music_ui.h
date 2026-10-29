#ifndef NDVIDEO_MUSIC_UI_H
#define NDVIDEO_MUSIC_UI_H
#include "player_internal.h"
#include "audio/audio_playback.h"
#include "music_controls.h"
#include "music_visual.h"
#include "audio/audio_metadata.h"
enum
{
    MUSIC_VIEW_ART,
    MUSIC_VIEW_FLOW,
    MUSIC_VIEW_VORTEX,
    MUSIC_VIEW_PRISM,
    MUSIC_VIEW_PULSE,
    MUSIC_VIEW_SCOPE,
    MUSIC_VIEW_SPECTRUM,
    MUSIC_VIEW_COUNT,
    MUSIC_COVER_SIDE = 216
};
typedef struct
{
    AudioMetadata tags;
    ScreenshotPreviewState screenshot;
    uint16_t *cover;
    MusicVisual *visual;
    UiTransition hover[MUSIC_CONTROL_COUNT], press[MUSIC_CONTROL_COUNT], info, queue, tooltip, art, chrome;
    char title[AUDIO_TAG_TEXT], artist[AUDIO_TAG_TEXT], album[AUDIO_TAG_TEXT];
    char previous[MAX_PATH_LEN], next[MAX_PATH_LEN];
    uint32_t chrome_until;
    uint32_t hover_at, visual_at, status_at, status_until, brightness_at, brightness_repeat_at;
    uint32_t brightness_from, brightness_to;
    int hovered, queue_target, tooltip_target, brightness_direction;
    unsigned view;
    PlaybackMode mode;
    bool animating, brightness_active;
    char status[64], cover_status[64], cover_path[MAX_PATH_LEN];
    uint32_t cover_read_ms, cover_decode_ms, render_calls, render_ms, render_max_ms;
    uint32_t visual_calls, visual_ms, visual_max_ms;
    uint32_t screen_offs,screen_wakes,screen_off_position,screen_wake_position;
    uint32_t reconnects,reconnect_attempts,reconnect_max_ms,reconnect_sample;
    bool cover_error;
} MusicUi;
void music_ui_open(MusicUi *, const char *, LoadingProgress *);
void music_ui_close(MusicUi *);
void music_ui_debug(FILE *, const void *);
void music_ui_update(MusicUi *, AudioPlayback *, const AudioPlaybackInfo *, const MusicControls *,
                     const PointerState *, uint32_t now);
void music_ui_render(SDL_Surface *, const Fonts *, const char *, AudioPlayback *,
                     const AudioPlaybackInfo *, const MusicControls *, const PointerState *,
                     MusicUi *, uint32_t);
void music_ui_status(MusicUi *, const char *, uint32_t);
void music_ui_brightness(MusicUi *, bool up, bool down, bool up_edge, bool down_edge, uint32_t);
void music_ui_cycle_view(MusicUi *);
int music_wait_for_audio(SDL_Surface *,const Fonts *,const char *,AudioPlayback **,
                         AudioPlaybackInfo *,MusicControls *,PointerState *,MusicUi *);
#endif
