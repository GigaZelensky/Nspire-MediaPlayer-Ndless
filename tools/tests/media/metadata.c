#include "audio/audio_metadata.h"
#include "player/cover_image.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef COVER_REFERENCE
uint16_t *cover_image_decode_reference(const void *,size_t,unsigned);
#endif

typedef struct
{
    FILE *file;
    uint64_t cursor, bytes;
    size_t read_bytes;
} Input;
static size_t read_bytes(void *context, void *out, size_t bytes)
{
    Input *in = context;
    assert(in->cursor <= in->bytes && bytes <= in->bytes - in->cursor);
    size_t n = fread(out, 1, bytes, in->file);
    in->cursor += n;
    in->read_bytes += n;
    return n;
}
static bool seek_bytes(void *context, uint64_t offset)
{
    Input *in = context;
    assert(offset <= in->bytes);
    in->cursor = offset;
    return fseek(in->file, (long)offset, SEEK_SET) == 0;
}
int main(int argc, char **argv)
{
    assert(argc >= 2);
    if (!strcmp(argv[1], "--image"))
    {
        assert(argc == 3);
        FILE *file = fopen(argv[2], "rb"); assert(file);
        fseek(file, 0, SEEK_END); long bytes = ftell(file); rewind(file);
        assert(bytes >= 0 && bytes <= 1024 * 1024);
        void *data = malloc(bytes ? (size_t)bytes : 1); assert(data);
        assert(fread(data, 1, (size_t)bytes, file) == (size_t)bytes); fclose(file);
        uint16_t *cover = cover_image_decode(data, (size_t)bytes, 216);
        bool ok = cover != NULL; free(cover); free(data);
        return ok ? 0 : 2;
    }
    Input in = {0};
    in.file = fopen(argv[1], "rb");
    assert(in.file);
    assert(fseek(in.file, 0, SEEK_END) == 0);
    in.bytes = (uint64_t)ftell(in.file);
    AudioInput input = {&in, read_bytes, seek_bytes, NULL, in.bytes};
    AudioMetadata tags;
    audio_metadata_read(&input, &tags);
    assert(in.read_bytes <= 2 * 1024 * 1024);
    assert(tags.art_bytes <= AUDIO_ART_LIMIT);
    fwrite(tags.title, 1, strlen(tags.title) + 1, stdout);
    fwrite(tags.artist, 1, strlen(tags.artist) + 1, stdout);
    fwrite(tags.album, 1, strlen(tags.album) + 1, stdout);
    if (argc > 2)
    {
        FILE *out = fopen(argv[2], "wb");
        assert(out);
        if (tags.art)
        {
            assert(fwrite(tags.art, 1, tags.art_bytes, out) == tags.art_bytes);
        }
        fclose(out);
    }
    if (tags.art)
    {
        uint16_t *thumb = cover_image_decode(tags.art, tags.art_bytes, 88);
        assert(thumb);
        bool visible = false;
        for (unsigned i = 0; i < 88 * 88; ++i)
            if (thumb[i])
                visible = true;
        assert(visible);
        free(thumb);
        thumb = cover_image_decode(tags.art, tags.art_bytes, 216);
        assert(thumb);
#ifdef COVER_REFERENCE
        uint16_t *reference=cover_image_decode_reference(tags.art,tags.art_bytes,216);
        assert(reference && !memcmp(thumb,reference,216*216*2));free(reference);
#endif
        if (argc > 3)
        {
            FILE *out = fopen(argv[3], "wb");
            assert(out);
            assert(fwrite(thumb, 2, 216 * 216, out) == 216 * 216);
            fclose(out);
        }
        free(thumb);
    }
    audio_metadata_close(&tags);
    assert(!tags.art && !tags.art_bytes);
    fclose(in.file);
    return 0;
}
