#ifndef NDVIDEO_AUDIO_DECODER_H
#define NDVIDEO_AUDIO_DECODER_H
#include "media_config.h"
#include "audio_buffer.h"
#include "input.h"
typedef struct {
    uint32_t encoded_bytes, read_ms, decode_ms;
    unsigned codec;
} AudioDecodeStats;
typedef struct AudioDecoder AudioDecoder;
AudioDecoder *audio_decoder_open(AudioBuffer *, AudioInput, AudioDecodeStats *, uint32_t (*)(void),
                                 unsigned, char *, size_t);
int audio_decoder_step(AudioDecoder *, uint64_t);
bool audio_decoder_eof(const AudioDecoder *);
const char *audio_decoder_error(const AudioDecoder *);
void audio_decoder_debug(const AudioDecoder *, FILE *);
void audio_decoder_close(AudioDecoder *);
bool audio_decoder_limit(AudioDecoder *, uint32_t);
bool audio_decoder_seek(AudioDecoder *, uint32_t);
#endif
