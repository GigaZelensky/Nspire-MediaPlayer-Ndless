#ifndef NDVIDEO_AUDIO_PLAYBACK_H
#define NDVIDEO_AUDIO_PLAYBACK_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
struct LoadingProgress;
typedef struct AudioPlayback AudioPlayback;

/* NULL selects an ordinary audio file. A movie supplies its validated bounded
 * soundtrack descriptor; neither path creates a second USB owner/decoder. */
typedef struct {
    uint64_t offset, bytes;
    uint32_t frames, rate;
    unsigned channels, codec;
} AudioPlaybackTrack;
typedef struct { unsigned volume; bool muted, failed; } AudioPlaybackLevel;
typedef struct {
    uint32_t rate, frames, position;
    unsigned channels, codec;
    bool duration_known, running, failed, finished;
} AudioPlaybackInfo;

AudioPlayback *audio_playback_open(const char *, const AudioPlaybackTrack *, struct LoadingProgress *);
void audio_playback_close(AudioPlayback *);
/* Automatic retry attempts share the original failure report instead of
 * appending one full report on every reconnect timer. All resources still close. */
void audio_playback_discard_retry(AudioPlayback *);
bool audio_playback_can_reconnect(const AudioPlayback *);
bool audio_playback_cancelled(const AudioPlayback *);
void audio_playback_tick_loading(AudioPlayback *, struct LoadingProgress *, bool,
                                 unsigned num, unsigned den);
void audio_playback_tick(AudioPlayback *, bool paused, unsigned num, unsigned den);
void audio_playback_idle(AudioPlayback *);
void audio_playback_pump(AudioPlayback *);
void audio_playback_pause(AudioPlayback *);
void audio_playback_suspend(AudioPlayback *);
bool audio_playback_seek(AudioPlayback *, uint32_t sample);
bool audio_playback_retry(AudioPlayback *, uint32_t sample);
bool audio_playback_active(const AudioPlayback *);
bool audio_playback_source_lost(const AudioPlayback *);
uint64_t audio_playback_position(const AudioPlayback *);
bool audio_playback_info(const AudioPlayback *, AudioPlaybackInfo *);
/* Read-only stereo PCM at the output clock. No decoding/I/O, no ring mutation.
 * Missing/paused data is zero-filled; optional visuals cannot fail playback. */
size_t audio_playback_snapshot(const AudioPlayback *, int16_t *stereo, size_t frames);
bool audio_playback_level(const AudioPlayback *, AudioPlaybackLevel *);
void audio_playback_set_volume(AudioPlayback *, unsigned);
void audio_playback_volume(AudioPlayback *, int);
void audio_playback_mute(AudioPlayback *);
const char *audio_playback_status(AudioPlayback *);
void audio_playback_debug(const AudioPlayback *, FILE *);
void audio_playback_context(AudioPlayback *, uint32_t position, uint32_t total, const char *codec);
void audio_playback_checkpoint(AudioPlayback *, const char *event);
void audio_playback_diagnostics(AudioPlayback *,void (*)(FILE *,const void *),const void *,bool error);
#endif
