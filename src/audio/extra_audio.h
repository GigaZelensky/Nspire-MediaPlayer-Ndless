#ifndef NDVIDEO_EXTRA_AUDIO_H
#define NDVIDEO_EXTRA_AUDIO_H
#include "input.h"

typedef struct ExtraAudio ExtraAudio;
typedef struct {
    int (*read)(ExtraAudio *, int16_t *, unsigned);
    bool (*seek)(ExtraAudio *, uint32_t);
    void (*close)(ExtraAudio *);
} ExtraAudioOps;
struct ExtraAudio {
    AudioInput input;
    const ExtraAudioOps *ops;
    void *state;
    uint32_t rate, frames;
    unsigned channels, codec;
    const char *error;
    const ExtraAudioOps *delegate_ops;
    unsigned module_slot;
};
typedef struct {
    uint32_t struct_size, audio_size, input_size, codec;
    ExtraAudio *(*open)(AudioInput);
} AudioModuleApi;
ExtraAudio *extra_audio_open(AudioInput, const uint8_t signature[4]);
ExtraAudio *extra_ogg_open(AudioInput);
ExtraAudio *extra_aac_open(AudioInput);
bool extra_input_seek(AudioInput *, int64_t, int whence);
void extra_audio_close(ExtraAudio *);
#endif
