#include "movie_file.h"
#include "storage_read_stream.h"
#include "raw_player_io.h"
#include "movie/nve_crypto.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Video streams use explicit 64-bit positions instead of Newlib's 32-bit
 * seek/tell ABI. Small metadata reads retain a bounded refill cache. */
struct MovieReadFile {
    StorageReadStream *stream;
    uint64_t position, cache_offset;
    uint32_t cached;
    unsigned char cache[1024];
};
MovieReadFile *movie_file_open(const char *path)
{
    MovieReadFile *file = calloc(1, sizeof(*file));
    if (!file) { errno = ENOMEM; return NULL; }
    int error = 0;
    file->stream = storage_read_stream_open(path, &error);
    if (!file->stream) { free(file); errno = error; return NULL; }
    return file;
}
uint64_t movie_file_size(const MovieReadFile *file)
{ return file ? storage_read_stream_size(file->stream) : 0; }
int movie_file_seek(MovieReadFile *file, uint64_t offset)
{
    if (!file || offset > movie_file_size(file)) { errno = EINVAL; return -1; }
    file->position = offset;
    return 0;
}
size_t movie_file_read(void *data, size_t size, size_t count, MovieReadFile *file)
{
    if (!size || !count) return 0;
    if (!file || !data || count > SIZE_MAX / size) { errno = EINVAL; return 0; }
    size_t remaining = size * count, copied = 0;
    uint64_t available = movie_file_size(file) - file->position;
    if (remaining > available) remaining = (size_t)available;
    while (remaining) {
        uint32_t bytes;
        int error = 0;
        if (file->cached && file->position >= file->cache_offset &&
            file->position - file->cache_offset < file->cached) {
            uint32_t within = (uint32_t)(file->position - file->cache_offset);
            bytes = file->cached - within;
            if (bytes > remaining) bytes = (uint32_t)remaining;
            memcpy((unsigned char *)data + copied, file->cache + within, bytes);
        } else if (remaining >= sizeof(file->cache)) {
            bytes = remaining > 32768U ? 32768U : (uint32_t)remaining;
            int read = storage_read_stream_read_at(file->stream, file->position,
                (unsigned char *)data + copied, bytes, &error);
            if (read != (int)bytes) { errno = error ? error : EIO; break; }
        } else {
            uint64_t tail = movie_file_size(file) - file->position;
            bytes = tail > sizeof(file->cache) ? sizeof(file->cache) : (uint32_t)tail;
            int read = storage_read_stream_read_at(file->stream, file->position, file->cache, bytes, &error);
            if (read != (int)bytes) { file->cached = 0; errno = error ? error : EIO; break; }
            file->cache_offset = file->position; file->cached = bytes;
            continue;
        }
        file->position += bytes; copied += bytes; remaining -= bytes;
    }
    return copied / size;
}
int movie_file_close(MovieReadFile *file)
{
    if (!file) { errno = EBADF; return -1; }
    int error = 0;
    raw_player_before_native();
    int result = storage_read_stream_close(file->stream, &error);
    if (!result || error != EBUSY) { nve_wipe(file, sizeof(*file)); free(file); }
    if (result) errno = error;
    return result;
}
