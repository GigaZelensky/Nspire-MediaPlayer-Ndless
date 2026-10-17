#ifndef NDVIDEO_MOVIE_AUDIO_H
#define NDVIDEO_MOVIE_AUDIO_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "audio_playback.h"
struct Movie;
struct LoadingProgress;
void movie_audio_open(struct Movie *, const char *, struct LoadingProgress *);
bool movie_audio_close(struct Movie *);
/* Explicit diagnostic boundary; healthy production playback does not write. */
void movie_audio_checkpoint(const struct Movie *);
void movie_audio_suspend(const struct Movie *);
void movie_audio_pause(const struct Movie *);
void movie_audio_reanchor(const struct Movie *);
void movie_audio_tick(const struct Movie *, bool, unsigned, unsigned);
void movie_audio_pump(const struct Movie *);
void movie_audio_idle(const struct Movie *);
bool movie_audio_master(const struct Movie *);
bool movie_audio_source_lost(const struct Movie *);
uint32_t movie_audio_frame(const struct Movie *);
uint32_t movie_audio_wait_ms(const struct Movie *, unsigned, unsigned);
const char *movie_audio_status(const struct Movie *);
void movie_audio_volume(const struct Movie *, int);
typedef AudioPlaybackLevel MovieAudioLevel;
bool movie_audio_level(const struct Movie *, MovieAudioLevel *);
void movie_audio_set_volume(const struct Movie *, unsigned);
void movie_audio_mute(const struct Movie *);
void movie_audio_debug(const struct Movie *, FILE *);
#endif
