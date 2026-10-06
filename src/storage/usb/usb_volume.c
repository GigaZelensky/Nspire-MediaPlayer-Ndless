#include "usb_storage.h"
#include "usb_controller.h"
#include "usb_fs_worker.h"
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* One read-only volume and one cooperative filesystem worker. A completed
 * request owns its result until collected; another reader cannot overwrite it. */
struct UsbFile {
    FIL file;
    uint32_t generation, bytes;
    unsigned readers;
};
struct UsbRead {
    UsbFsJob job;
    UsbFile *file;
    uint32_t offset, bytes;
    void *destination;
    int result;
};
static FATFS volume;
static uint32_t mounted_generation;
static char root[32];
static bool mounted;

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
    /* Keep all access inside /ndvideo, including manually supplied paths. */
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
    int n = snprintf(out, capacity, "0:/ndvideo%s", relative);
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
    if (drive || !count || !data || !size || sector > UINT32_MAX ||
        sector > usb_controller_last_sector() || count - 1 > usb_controller_last_sector() - sector)
        return RES_PARERR;
    while (count) {
        if (usb_fs_cancelled() || !usb_controller_online() ||
            generation != usb_controller_generation())
            return RES_ERROR;
        unsigned amount = count;
        if (amount > 4096U / size)
            amount = 4096U / size;
        UsbCommand request = {0};
        while (!usb_controller_begin_read(&request, (uint32_t)sector, amount, data)) {
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
        r = f_opendir(&dir, "0:/ndvideo");
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
                                     ? "Create an ndvideo folder on the USB drive."
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
    if (r == FR_OK && f_size(&job->file->file) > UINT32_MAX) {
        f_close(&job->file->file);
        job->job.error = EFBIG;
        return;
    }
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
    file->bytes = (uint32_t)f_size(&file->file);
    file->generation = mounted_generation;
    return file;
}
uint32_t usb_file_size(const UsbFile *file)
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
    FRESULT r = f_lseek(&file->file, read->offset);
    UINT bytes = 0;
    if (r == FR_OK)
        r = f_read(&file->file, read->destination, read->bytes, &bytes);
    if (read->job.cancelled && live(file) && usb_controller_online()) {
        /* Our cooperative cancellation is represented as a disk error inside
         * FatFs. It is safe to clear only this software-generated error; a
         * genuine USB failure marks the controller offline. */
        file->file.err = 0;
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
bool usb_read_begin(UsbRead *read, uint32_t offset, void *destination, uint32_t bytes)
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
int usb_file_read_at(UsbFile *file, uint32_t offset, void *destination, uint32_t bytes, int *error)
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
        if (job.info.fname[0] == '.' || (job.info.fattrib & (AM_HID | AM_SYS)) ||
            job.info.fsize > UINT32_MAX)
            continue;
        /* The visitor can allocate and render: it runs on the main stack. */
        if (!callback(context, job.info.fname, (job.info.fattrib & AM_DIR) != 0,
                      (uint32_t)job.info.fsize))
            break;
    }
    int status = job.job.error;
    job.operation = 2;
    run_sync(&job.job);
    if (error)
        *error = status;
    return ok;
}
