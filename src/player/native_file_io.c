#include "native_file_io.h"
#include "native_interrupts.h"
#include "native_writer_platform.h"
#include <string.h>

typedef struct {
    void *(*file_open)(const char *, const char *);
    unsigned (*file_write)(const void *, unsigned, unsigned, void *);
    int (*file_close)(void *);
    int (*file_remove)(const char *);
    int (*file_stat)(const char *, void *);
    int *(*errno_address)(void);
} NativeFileApi;
static NativeFileApi native_api;
static struct { int status; uint32_t mask, task, magic, flags, slice, policy; } init_state;
void native_file_io_debug(FILE *file)
{
    if (file) fprintf(file, "native_file_writer init=%d mask=%02lx task=%08lx magic=%08lx flags=%08lx slice=%lu policy=%08lx\n",
        init_state.status, (unsigned long)init_state.mask, (unsigned long)init_state.task,
        (unsigned long)init_state.magic, (unsigned long)init_state.flags,
        (unsigned long)init_state.slice, (unsigned long)init_state.policy);
}
static int file_platform(void)
{
    const NativeWriterApi *api = native_writer_platform();
    if (!api) return NATIVE_FILE_IO_UNSUPPORTED;
    native_api = (NativeFileApi){
        (void *(*)(const char *, const char *))(uintptr_t)api->function[WR_OPEN],
        (unsigned (*)(const void *, unsigned, unsigned, void *))(uintptr_t)api->function[WR_WRITE],
        (int (*)(void *))(uintptr_t)api->function[WR_CLOSE],
        (int (*)(const char *))(uintptr_t)api->function[WR_REMOVE],
        (int (*)(const char *, void *))(uintptr_t)api->function[WR_STAT],
        (int *(*)(void))(uintptr_t)api->function[WR_ERRNO]};
    if (!native_writer_span(api->task_global, 4) || !native_writer_span(api->interrupt_policy, 4))
        return NATIVE_FILE_IO_BAD_TASK;
    uintptr_t address = *(const volatile uint32_t *)(uintptr_t)api->task_global;
    init_state.task = (uint32_t)address;
    if (!native_writer_span((uint32_t)address, 0x44)) return NATIVE_FILE_IO_BAD_TASK;
    const volatile unsigned char *task = (const volatile unsigned char *)address;
    init_state.magic = *(const volatile uint32_t *)(task + 0x0c);
    init_state.flags = *(const volatile uint32_t *)(task + 0x18);
    init_state.slice = *(const volatile uint32_t *)(task + 0x40);
    init_state.policy = *(const volatile uint32_t *)(uintptr_t)api->interrupt_policy;
    if (*(const volatile unsigned *)(task + 0x0c) != 0x5441534BU || !task[0x1b] ||
        !*(const volatile unsigned *)(task + 0x40) ||
        *(const volatile uint32_t *)(uintptr_t)api->interrupt_policy != 0U)
        return NATIVE_FILE_IO_BAD_TASK;
    return NATIVE_FILE_IO_OK;
}
int native_file_io_init(NativeFileIo *backend)
{
    if (!backend)
        return NATIVE_FILE_IO_BAD_STATE;
    if (backend->io_phase)
        return NATIVE_FILE_IO_BUSY;
    unsigned saved = native_interrupt_mask();
    memset(backend, 0, sizeof(*backend));
    memset(&init_state, 0, sizeof(init_state));
    init_state.mask = saved;
    int result = file_platform();
    init_state.status = result;
    native_critical_leave(saved);
    if (result)
        return result;
    if (!(native_interrupt_mask() & 0x80U)) {
        init_state.status = NATIVE_FILE_IO_BAD_TASK;
        return NATIVE_FILE_IO_BAD_TASK;
    }
    backend->supported = true;
    return NATIVE_FILE_IO_OK;
}

void *native_file_io_open(NativeFileIo *backend, const char *path)
{
    if (!backend || !backend->supported || !path)
        return NULL;
    backend->io_phase = 4U;
    void *file = native_api.file_open(path, "rb");
    backend->io_phase = 0;
    return file;
}

void *native_file_io_open_write(NativeFileIo *backend, const char *path)
{
    if (!backend || !backend->supported || !path)
        return NULL;
    backend->io_phase = 4U;
    void *file = native_api.file_open(path, "wb");
    backend->io_phase = 0;
    return file;
}

void *native_file_io_open_append(NativeFileIo *backend, const char *path)
{
    if (!backend || !backend->supported || !path)
        return NULL;
    backend->io_phase = 4U;
    void *file = native_api.file_open(path, "ab");
    backend->io_phase = 0;
    return file;
}

size_t native_file_io_write(NativeFileIo *backend, void *file, const void *buffer, size_t bytes)
{
    unsigned requested = (unsigned)bytes;
    if (!backend || !backend->supported || !file || !buffer || (size_t)requested != bytes)
        return 0;
    backend->io_phase = 2U;
    size_t result = (size_t)native_api.file_write(buffer, 1U, requested, file);
    backend->io_phase = 0;
    return result;
}

int native_file_io_close(NativeFileIo *backend, void *file)
{
    if (!backend || !backend->supported || !file)
        return NATIVE_FILE_IO_BAD_STATE;
    backend->io_phase = 8U;
    int result = native_api.file_close(file);
    backend->io_phase = 0;
    return result;
}

int native_file_io_exists(NativeFileIo *backend, const char *path, int *native_error)
{
    /* The verified native stat writes through offset32 (36 bytes total).
     * Keep an aligned opaque buffer so this never depends on newlib's stat ABI. */
    unsigned information[16];
    int *task_errno;
    int result, error;
    if (native_error)
        *native_error = 0;
    if (!backend || !backend->supported || !path) {
        if (native_error)
            *native_error = NATIVE_FILE_IO_BAD_STATE;
        return -1;
    }
    /* errno_addr indexes the current native task's 812-byte runtime record.
     * It is independent of newlib errno and of the other application context. */
    task_errno = native_api.errno_address();
    if (!task_errno) {
        if (native_error)
            *native_error = NATIVE_FILE_IO_BAD_STATE;
        return -1;
    }
    *task_errno = 0;
    backend->io_phase = 16U;
    result = native_api.file_stat(path, information);
    backend->io_phase = 0;
    error = *task_errno;
    if (result == 0)
        return 1;
    if (native_error)
        *native_error = error;
    return error == 2 ? 0 : -1; /* Native ENOENT is explicitly stored as2. */
}

int native_file_io_remove(NativeFileIo *backend, const char *path)
{
    if (!backend || !backend->supported || !path)
        return NATIVE_FILE_IO_BAD_STATE;
    backend->io_phase = 64U;
    int result = native_api.file_remove(path);
    backend->io_phase = 0;
    return result;
}
