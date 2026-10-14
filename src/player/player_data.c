#include "player_data.h"
#include "raw_player_io.h"
#include <libndls.h>
#include <os.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

static char data_directory[512] = "./" PLAYER_DATA_FOLDER;
static bool directory_ready;

void player_data_configure(const char *app_path)
{
    directory_ready = false;
    const char *slash = app_path ? strrchr(app_path, '/') : NULL;
    const char *backslash = app_path ? strrchr(app_path, '\\') : NULL;
    if (backslash && (!slash || backslash > slash))
        slash = backslash;
    size_t length = slash ? (size_t)(slash - app_path) : 1;
    const char *parent = slash ? app_path : ".";
    if (slash == app_path && slash)
        length = 1;
    if (length + sizeof("/" PLAYER_DATA_FOLDER) > sizeof(data_directory)) {
        data_directory[0] = 0;
        return;
    }
    memcpy(data_directory, parent, length);
    for (size_t index = 0; index < length; ++index)
        if (data_directory[index] == '\\')
            data_directory[index] = '/';
    if (data_directory[length - 1] != '/')
        data_directory[length++] = '/';
    strcpy(data_directory + length, PLAYER_DATA_FOLDER);
}

const char *player_data_directory(void)
{
    return data_directory;
}

bool player_data_path(const char *name, char *path, size_t bytes)
{
    if (!path || !bytes)
        return false;
    path[0] = 0;
    if (!data_directory[0] || !name || !*name || strchr(name, '/') || strchr(name, '\\'))
        return false;
    int length = snprintf(path, bytes, "%s/%s", data_directory, name);
    if (length < 0 || (size_t)length >= bytes) {
        path[0] = 0;
        return false;
    }
    return true;
}

bool player_data_prepare(void)
{
    if (directory_ready)
        return true;
    if (!data_directory[0])
        return false;
    /* Directory creation mutates the filesystem just like a foreground write. */
    raw_player_before_native();
    if (mkdir(data_directory, 0777) == 0)
        return directory_ready = true;
    struct nuc_stat status;
    directory_ready =
        nuc_stat(data_directory, &status) == 0 && (status.st_mode & 0170000) == 0040000;
    return directory_ready;
}
