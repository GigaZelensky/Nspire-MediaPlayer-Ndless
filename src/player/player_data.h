#ifndef NDVIDEO_PLAYER_DATA_H
#define NDVIDEO_PLAYER_DATA_H
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define PLAYER_DATA_FOLDER "ndvideo-data"

void player_data_configure(const char *app_path);
const char *player_data_directory(void);
bool player_data_path(const char *name, char *path, size_t bytes);
/* Create only when writing history or diagnostics, before starting a writer. */
bool player_data_prepare(void);
/* Automatic reports keep a bounded recent history; D's recording is separate. */
FILE *player_error_log_open(const char *path,bool append);
#endif
