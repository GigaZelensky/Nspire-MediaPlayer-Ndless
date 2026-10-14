#ifndef NDVIDEO_MOVIE_FILE_H
#define NDVIDEO_MOVIE_FILE_H
#include <stdint.h>
#include <stddef.h>
typedef struct MovieReadFile MovieReadFile;
MovieReadFile *movie_file_open(const char *path);
uint64_t movie_file_size(const MovieReadFile *file);
int movie_file_seek(MovieReadFile *file, uint64_t offset);
size_t movie_file_read(void *data, size_t size, size_t count, MovieReadFile *file);
int movie_file_close(MovieReadFile *file);
#endif
