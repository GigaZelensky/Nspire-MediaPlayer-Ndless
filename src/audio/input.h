#ifndef ND_AUDIO_INPUT_H
#define ND_AUDIO_INPUT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    void *context;
    size_t (*read)(void *, void *, size_t);
    bool (*seek)(void *, uint64_t);
    uint64_t (*tell)(void *);
    uint64_t bytes;
} AudioInput;
#endif
