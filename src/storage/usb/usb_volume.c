#include "usb_storage.h"
#include "usb_controller.h"
#include "usb_fs_worker.h"
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* One read-only volume and one cooperative filesystem worker. A completed
 * request owns its result until collected; another reader cannot overwrite it. */
struct UsbFile {
    FIL file;
    uint32_t generation;
    uint64_t bytes;
    unsigned readers;
};
struct UsbRead {
    UsbFsJob job;
    UsbFile *file;
    uint64_t offset;
    uint32_t bytes;
    void *destination;
    int result;
};
static FATFS volume;
static uint32_t mounted_generation;
static char root[32];
static bool mounted;
static const char *media_folder="nMedia";

static int failure(FRESULT r)
{
    switch (r) {
    case FR_OK:
        return 0;
    case FR_NO_FILE:
    case FR_NO_PATH:
        return ENOENT;
    case FR_INVALID_NAME:
    case FR_INVALID_PARAMETER:
        return EINVAL;
    case FR_NOT_READY:
    case FR_INVALID_OBJECT:
        return ENODEV;
    case FR_NOT_ENOUGH_CORE:
        return ENOMEM;
    case FR_NO_FILESYSTEM:
        return ENOTSUP;
    case FR_DENIED:
        return EACCES;
    case FR_TOO_MANY_OPEN_FILES:
        return EMFILE;
    default:
        return EIO;
    }
}
bool usb_path(const char *path)
{
    return path && !strncmp(path, "usb:/", 5);
}
bool usb_storage_mounted(void)
{
    if(!NDMEDIA_USB_STORAGE)return false;
    usb_storage_poll_connection();
    return mounted && usb_controller_online() && mounted_generation == usb_controller_generation();
}
const char *usb_storage_root(void)
{
    return usb_storage_mounted() ? root : NULL;
}
static bool live(const UsbFile *file)
{
    return file && usb_storage_mounted() && file->generation == mounted_generation;
}
static bool relative_path(const char *path, char *out, size_t capacity)
{
    if (!usb_storage_mounted() || !path)
        return false;
    size_t prefix = strlen(root);
    if (strncmp(path, root, prefix) || (path[prefix] && path[prefix] != '/'))
        return false;
    const char *relative = path + prefix;
    /* Keep all access inside the mounted media folder, including supplied paths. */
    for (const char *p = relative; *p;) {
        if (*p++ != '/')
            return false;
        const char *start = p;
        while (*p && *p != '/') {
            if (*p == '\\' || *p == ':' || (unsigned char)*p < 32)
                return false;
            ++p;
        }
        size_t n = (size_t)(p - start);
        if (!n || (n == 1 && start[0] == '.') || (n == 2 && start[0] == '.' && start[1] == '.'))
            return false;
    }
    int n = snprintf(out, capacity, "0:/%s%s", media_folder,relative);
    return n >= 0 && (size_t)n < capacity;
}

DSTATUS disk_status(BYTE drive)
{
    return drive || !usb_controller_online() ? STA_NOINIT | STA_NODISK : STA_PROTECT;
}
DSTATUS disk_initialize(BYTE drive)
{
    return disk_status(drive);
}
DRESULT disk_write(BYTE drive, const BYTE *data, LBA_t sector, UINT count)
{
    (void)drive;
    (void)data;
    (void)sector;
    (void)count;
    return RES_WRPRT;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *data)
{
    if (drive || !usb_controller_online())
        return RES_NOTRDY;
    if (!data)
        return RES_PARERR;
    if (command == GET_SECTOR_SIZE) {
        *(WORD *)data = (WORD)usb_controller_sector_size();
        return RES_OK;
    }
    if (command == GET_SECTOR_COUNT) {
        *(LBA_t *)data = (LBA_t)usb_controller_last_sector() + 1;
        return RES_OK;
    }
    if (command == GET_BLOCK_SIZE) {
        *(DWORD *)data = 1;
        return RES_OK;
    }
    return RES_PARERR;
}
DRESULT disk_read(BYTE drive, BYTE *data, LBA_t sector, UINT count)
{
    uint32_t size = usb_controller_sector_size(), generation = usb_controller_generation();
    if (drive || !count || !data || !size ||
        sector > usb_controller_last_sector() || count - 1 > usb_controller_last_sector() - sector)
        return RES_PARERR;
    while (count) {
        if (usb_fs_cancelled() || !usb_controller_online() ||
            generation != usb_controller_generation())
            return RES_ERROR;
        unsigned amount = count;
        if (amount > usb_controller_max_read_bytes() / size)
            amount = usb_controller_max_read_bytes() / size;
        UsbCommand request = {0};
        while (!usb_controller_begin_read(&request, sector, amount, data)) {
            if (!usb_controller_online() || usb_fs_cancelled())
                return RES_ERROR;
            usb_fs_yield();
        }
        int result;
        /* Complete CBW/data/CSW even after cancellation: the next command must
         * start at a transaction boundary, never in the preceding data phase. */
        while (!(result = usb_controller_step(&request)))
            usb_fs_yield();
        if (result < 0 || generation != usb_controller_generation())
            return RES_ERROR;
        data += amount * size;
        sector += amount;
        count -= amount;
        if (!ndvideo_usb_fs_poll())
            return RES_ERROR;
    }
    return RES_OK;
}

static bool run_sync(UsbFsJob *job)
{
    /* Metadata requests may follow an async read whose result has not yet
     * been collected. Finish it without discarding its private result. */
    while (!usb_fs_idle())
        usb_fs_service(8);
    if (!usb_fs_submit(job))
        return false;
    usb_fs_wait(job);
    return job->done && !job->error;
}
typedef struct {
    UsbFsJob job;
    DWORD serial;
} MountJob;
static void mount_job(void *argument)
{
    MountJob *job = argument;
    FRESULT r = f_mount(&volume, "0:", 1);
    if (r == FR_OK)
        r = f_getlabel("0:", NULL, &job->serial);
    DIR dir;
    if (r == FR_OK) {
        media_folder="nMedia";
        r = f_opendir(&dir, "0:/nMedia");
        if(r==FR_NO_PATH || r==FR_NO_FILE) {
            media_folder="ndvideo"; /* Existing libraries keep working on upgrade. */
            r=f_opendir(&dir,"0:/ndvideo");
        }
        if (r == FR_OK)
            f_closedir(&dir);
    }
    job->job.error = failure(r);
}
bool usb_volume_mount(int *error)
{
    if (error)
        *error = 0;
    usb_volume_unmount();
    if (!usb_controller_online()) {
        if (error)
            *error = ENODEV;
        return false;
    }
    MountJob job = {0};
    job.job.run = mount_job;
    job.job.argument = &job;
    bool ok = run_sync(&job.job);
    if (ok) {
        mounted = true;
        mounted_generation = usb_controller_generation();
        snprintf(root, sizeof(root), "usb:/%08lx", (unsigned long)job.serial);
    } else {
        f_mount(NULL, "0:", 0);
        if (error)
            *error = job.job.error ? job.job.error : EIO;
        usb_controller_set_error(job.job.error == ENOENT
                                     ? "Create an nMedia folder on the USB drive."
                                     : "Could not read the USB filesystem.",
                                 job.job.error);
    }
    return ok;
}
void usb_volume_unmount(void)
{
    usb_fs_shutdown();
    f_mount(NULL, "0:", 0);
    mounted = false;
    root[0] = 0;
}
typedef struct {
    UsbFsJob job;
    UsbFile *file;
    char path[1024];
} OpenJob;
static void open_job(void *argument)
{
    OpenJob *job = argument;
    FRESULT r = f_open(&job->file->file, job->path, FA_READ);
    job->job.error = failure(r);
}
UsbFile *usb_file_open(const char *path, int *error)
{
    if (error)
        *error = 0;
    OpenJob job = {0};
    if (!relative_path(path, job.path, sizeof(job.path))) {
        if (error)
            *error = usb_storage_mounted() ? EINVAL : ENODEV;
        return NULL;
    }
    UsbFile *file = calloc(1, sizeof(*file));
    if (!file) {
        if (error)
            *error = ENOMEM;
        return NULL;
    }
    job.file = file;
    job.job.run = open_job;
    job.job.argument = &job;
    if (!run_sync(&job.job)) {
        if (error)
            *error = job.job.error ? job.job.error : EIO;
        free(file);
        return NULL;
    }
    file->bytes = (uint64_t)f_size(&file->file);
    file->generation = mounted_generation;
    return file;
}
uint64_t usb_file_size(const UsbFile *file)
{
    return file ? file->bytes : 0;
}
size_t usb_file_memory(const UsbFile *file)
{
    return file ? sizeof(*file) : 0;
}
void usb_file_close(UsbFile *file)
{
    if (!file || file->readers)
        return;
    /* Read-only FatFs has no close-time I/O or shared lock table. */
    if (live(file))
        f_close(&file->file);
    free(file);
}
static void read_job(void *argument)
{
    UsbRead *read = argument;
    UsbFile *file = read->file;
    if (!live(file)) {
        read->job.error = ENODEV;
        return;
    }
#if FF_FS_TINY
    FIL checkpoint = file->file;
#else
    /* Save the small cursor/object state, not the up-to-4-KiB sector buffer.
     * read_job always seeks before reading, so invalidating the cache on a
     * rollback forces a fresh fill without taxing every normal read. */
    unsigned char checkpoint[offsetof(FIL, buf)];
    memcpy(checkpoint, &file->file, sizeof(checkpoint));
#endif
    FRESULT r = f_lseek(&file->file, read->offset);
    UINT bytes = 0;
    if (r == FR_OK)
        r = f_read(&file->file, read->destination, read->bytes, &bytes);
    if (read->job.cancelled && live(file) && usb_controller_online()) {
        /* FatFs can abort after overwriting its sector buffer but before
         * updating the corresponding sector number. Clearing err alone then
         * exposes those bytes as the old cached sector on a later seek.
         * Restore the read-only cursor/object state and invalidate the cache.
         * The worker has retired the disk command before returning.
         * A real transport failure/offline volume is not recoverable here. */
#if FF_FS_TINY
        file->file = checkpoint;
#else
        memcpy(&file->file, checkpoint, sizeof(checkpoint));
        file->file.sect = 0; /* Not a file-data sector; f_lseek must refill it. */
#endif
        read->job.error = ECANCELED;
        return;
    }
    read->job.error = failure(r);
    if (r == FR_OK && bytes != read->bytes)
        read->job.error = EIO;
    read->result = read->job.error ? -1 : (int)bytes;
}
UsbRead *usb_read_create(UsbFile *file)
{
    if (!live(file))
        return NULL;
    UsbRead *read = calloc(1, sizeof(*read));
    if (!read)
        return NULL;
    read->file = file;
    read->job.run = read_job;
    read->job.argument = read;
    ++file->readers;
    return read;
}
bool usb_read_begin(UsbRead *read, uint64_t offset, void *destination, uint32_t bytes)
{
    if (!read || read->job.pending)
        return false;
    read->job.error = 0;
    if (!live(read->file)) {
        read->job.error = ENODEV;
        return false;
    }
    if (bytes > INT_MAX || offset > read->file->bytes || bytes > read->file->bytes - offset ||
        (bytes && (!destination || (uintptr_t)destination > UINTPTR_MAX - bytes))) {
        read->job.error = EINVAL;
        return false;
    }
    read->offset = offset;
    read->bytes = bytes;
    read->destination = destination;
    read->result = 0;
    return usb_fs_submit(&read->job);
}
int usb_read_step(UsbRead *read)
{
    if (!read)
        return -1;
    if (read->job.pending)
        usb_fs_service(8);
    if (read->job.error)
        return -1;
    return read->job.done ? 1 : 0;
}
int usb_read_error(const UsbRead *read)
{
    return read ? read->job.error : EINVAL;
}
void usb_read_cancel(UsbRead *read)
{
    if (!read)
        return;
    usb_fs_cancel(&read->job);
    usb_fs_wait(&read->job);
}
void usb_read_destroy(UsbRead *read)
{
    if (!read)
        return;
    usb_read_cancel(read);
    --read->file->readers;
    free(read);
}
int usb_file_read_at(UsbFile *file, uint64_t offset, void *destination, uint32_t bytes, int *error)
{
    if (error)
        *error = 0;
    UsbRead read = {0};
    read.file = file;
    read.job.run = read_job;
    read.job.argument = &read;
    while (!usb_fs_idle())
        usb_fs_service(8);
    if (!usb_read_begin(&read, offset, destination, bytes)) {
        if (error)
            *error = read.job.error ? read.job.error : EBUSY;
        return -1;
    }
    usb_fs_wait(&read.job);
    if (error)
        *error = read.job.error;
    return read.job.error ? -1 : read.result;
}
typedef struct {
    UsbFsJob job;
    DIR dir;
    FILINFO info;
    char path[1024];
    unsigned operation;
} DirectoryJob;
static void directory_job(void *argument)
{
    DirectoryJob *job = argument;
    FRESULT r = job->operation == 0   ? f_opendir(&job->dir, job->path)
                : job->operation == 1 ? f_readdir(&job->dir, &job->info)
                                      : f_closedir(&job->dir);
    job->job.error = failure(r);
}

struct UsbDirectory {
    DirectoryJob work;
    uint32_t generation;
    bool submitted, opened, complete;
};

UsbDirectory *usb_directory_open(const char *path, int *error)
{
    if (error) *error = 0;
    UsbDirectory *cursor = calloc(1, sizeof(*cursor));
    if (!cursor) {
        if (error) *error = ENOMEM;
        return NULL;
    }
    if (!relative_path(path, cursor->work.path, sizeof(cursor->work.path))) {
        if (error) *error = usb_storage_mounted() ? EINVAL : ENODEV;
        free(cursor);
        return NULL;
    }
    cursor->generation = mounted_generation;
    cursor->work.job.run = directory_job;
    cursor->work.job.argument = &cursor->work;
    return cursor;
}

int usb_directory_next(UsbDirectory *cursor, const char **name, bool *folder, int *error)
{
    if (error) *error = 0;
    if (!cursor || !name || !folder) {
        if (error) *error = EINVAL;
        return -1;
    }
    if (!usb_storage_mounted() || cursor->generation != mounted_generation) {
        if (error) *error = ENODEV;
        return -1;
    }
    if (cursor->complete)
        return 2;
    DirectoryJob *work = &cursor->work;
    if (!cursor->submitted) {
        if (!usb_fs_submit(&work->job)) {
            if (error) *error = work->job.error;
            return work->job.error ? -1 : 0;
        }
        cursor->submitted = true;
    }
    usb_fs_service(8U);
    if (work->job.pending)
        return 0;
    if (work->job.error) {
        if (error) *error = work->job.error;
        return -1;
    }
    cursor->submitted = false;
    if (!cursor->opened) {
        cursor->opened = true;
        work->operation = 1;
        return 0;
    }
    if (!work->info.fname[0]) {
        cursor->complete = true;
        return 2;
    }
    if (work->info.fname[0] == '.' || (work->info.fattrib & (AM_HID | AM_SYS)))
        return 0;
    *name = work->info.fname;
    *folder = (work->info.fattrib & AM_DIR) != 0;
    return 1;
}

void usb_directory_close(UsbDirectory *cursor)
{
    if (!cursor)
        return;
    if (cursor->work.job.pending) {
        usb_fs_cancel(&cursor->work.job);
        usb_fs_wait(&cursor->work.job);
    }
    /* Read-only FatFs close has no I/O or shared lock bookkeeping. */
    if (cursor->opened && usb_storage_mounted() && cursor->generation == mounted_generation)
        f_closedir(&cursor->work.dir);
    free(cursor);
}

bool usb_directory_list(const char *path, UsbDirectoryEntry callback, void *context, int *error)
{
    if (error)
        *error = 0;
    DirectoryJob job = {0};
    if (!callback || !relative_path(path, job.path, sizeof(job.path))) {
        if (error)
            *error = usb_storage_mounted() ? EINVAL : ENODEV;
        return false;
    }
    uint32_t generation = mounted_generation;
    job.job.run = directory_job;
    job.job.argument = &job;
    if (!run_sync(&job.job)) {
        if (error)
            *error = job.job.error;
        return false;
    }
    job.operation = 1;
    bool ok = true;
    for (;;) {
        if (!usb_storage_mounted() || generation != mounted_generation) {
            job.job.error = ENODEV;
            ok = false;
            break;
        }
        if (!run_sync(&job.job)) {
            ok = false;
            break;
        }
        if (!job.info.fname[0])
            break;
        if (job.info.fname[0] == '.' || (job.info.fattrib & (AM_HID | AM_SYS)))
            continue;
        /* The visitor can allocate and render: it runs on the main stack. */
        if (!callback(context, job.info.fname, (job.info.fattrib & AM_DIR) != 0,
                      (uint64_t)job.info.fsize))
            break;
    }
    int status = job.job.error;
    job.operation = 2;
    run_sync(&job.job);
    if (error)
        *error = status;
    return ok;
}
