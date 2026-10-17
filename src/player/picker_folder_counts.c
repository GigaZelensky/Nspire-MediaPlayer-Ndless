#include "player_internal.h"
#include "player_data.h"
#include "media_files.h"
#include "../storage/usb/usb_storage.h"

static MovieFile *counting;
static DIR *local_directory;
static UsbDirectory *usb_directory;
static uint32_t pending_files,pending_folders;

bool movie_picker_metadata_pending(const MovieFile *file)
{
    if(file && file->path && usb_path(file->path) && !usb_storage_mounted())return false;
    return file && (file->kind == PICKER_FOLDER ? !file->folder_count_checked
                                              : !file->timing_checked);
}

void picker_folder_counts_stop(void)
{
    if (usb_directory) usb_directory_close(usb_directory);
    if (local_directory) closedir(local_directory);
    usb_directory = NULL;
    local_directory = NULL;
    counting = NULL;
}

static void count_finished(bool valid)
{
    counting->folder_count_checked = true;
    if(valid) {
        bool changed=!counting->folder_count_valid || counting->folder_files!=pending_files ||
            counting->folder_folders!=pending_folders;
        counting->folder_files=pending_files;counting->folder_folders=pending_folders;
        counting->folder_count_valid=true;
        if(changed)counting->folder_count_ready_ms = monotonic_clock_now_ms();
    }
    picker_folder_counts_stop();
}

void picker_folder_counts_step(MovieFile *file)
{
    if (!file || file->kind != PICKER_FOLDER || file->folder_count_checked)
        return;
    if (counting != file) {
        picker_folder_counts_stop();
        counting = file;
        pending_files=pending_folders=0;
        if (usb_path(file->path)) {
            int error = 0;
            usb_directory = usb_directory_open(file->path, &error);
        } else
            local_directory = opendir(file->path);
        if (!usb_directory && !local_directory)
            count_finished(false);
        return;
    }
    const char *name;
    bool folder;
    if (usb_directory) {
        int error = 0;
        int result = usb_directory_next(usb_directory, &name, &folder, &error);
        if (!result) return;
        if (result != 1) {
            count_finished(result == 2);
            return;
        }
    } else {
        struct dirent *entry = readdir(local_directory);
        if (!entry) {
            count_finished(true);
            return;
        }
        name = entry->d_name;
        if (name[0] == '.') return;
        char path[MAX_PATH_LEN];
        int length = snprintf(path, sizeof(path), "%s/%s", file->path, name);
        if (length < 0 || (size_t)length >= sizeof(path) || !strcmp(path, player_data_directory()))
            return;
        struct nuc_stat status;
        if (nuc_stat(path, &status)) return;
        folder = (status.st_mode & 0170000) == 0040000;
    }
    if (folder) {
        if (pending_folders != UINT32_MAX) ++pending_folders;
    } else if (media_is_playable(name)) {
        if (pending_files != UINT32_MAX) ++pending_files;
    }
}
