#ifndef NDVIDEO_AUDIO_METADATA_H
#define NDVIDEO_AUDIO_METADATA_H
#include "media_config.h"
#include "input.h"

enum
{
    AUDIO_TAG_TEXT = 192,
    AUDIO_ART_LIMIT = 512 * 1024
};
typedef struct
{
    char title[AUDIO_TAG_TEXT], artist[AUDIO_TAG_TEXT], album[AUDIO_TAG_TEXT];
    uint8_t *art;
    size_t art_bytes;
    unsigned art_type;
    uint64_t file_bytes;
} AudioMetadata;
/* Bounded optional metadata. Bad/missing tags never prevent music playback. */
void audio_metadata_read(AudioInput *, AudioMetadata *);
void audio_metadata_close(AudioMetadata *);
/* Also used for conventional folder.jpg / cover.jpg, before playback starts. */
bool audio_metadata_art(AudioMetadata *, const void *, size_t, unsigned type);
void audio_metadata_display_text(const char *, char *, size_t);
#endif
