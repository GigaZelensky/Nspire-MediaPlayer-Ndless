#include "usb_fs_worker.h"
#include "usb_controller.h"
#include "usb_storage.h"
#include "../../player/native_interrupts.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define STACK_WORDS (16384U / sizeof(uint32_t))
#define GUARD_WORDS 8U
#define GUARD_VALUE 0x55425347U
typedef struct {
    UsbFsContext context;
    UsbFsJob *job;
    uint32_t started, budget;
    bool running, broken;
    uint32_t stack[STACK_WORDS + 2 * GUARD_WORDS];
} UsbFsWorker;
static UsbFsWorker *worker;
static void (*progress_callback)(void *);
static void *progress_context;
static uint32_t progress_last;
static bool progress_cancelled;
static uint32_t tick(void)
{
    return usb_controller_ticks();
}
static bool guards(const UsbFsWorker *w)
{
    for (unsigned i = 0; i < GUARD_WORDS; ++i)
        if (w->stack[i] != GUARD_VALUE || w->stack[STACK_WORDS + GUARD_WORDS + i] != GUARD_VALUE)
            return false;
    return true;
}
static void stack_failure(void)
{
    /* Keep the damaged stack allocated: a command may still refer to its
     * request object. Retire DMA before releasing the caller's job/buffer. */
    worker->broken = true;
    while (!usb_storage_park()) {
    }
    if (worker->job) {
        worker->job->error = EOVERFLOW;
        worker->job->pending = false;
        worker->job->done = true;
        worker->job = NULL;
    }
}
static void entry(void *argument)
{
    UsbFsWorker *w = argument;
    for (;;) {
        UsbFsJob *job = w->job;
        if (job) {
            if (!job->cancelled)
                job->run(job->argument);
            if (job->cancelled)
                job->error = ECANCELED;
            job->pending = false;
            job->done = true;
            w->job = NULL;
        }
        usb_fs_context_yield(&w->context);
    }
}
bool usb_fs_submit(UsbFsJob *job)
{
    if (!job || !job->run || job->pending)
        return false;
    if (!worker) {
        worker = calloc(1, sizeof(*worker));
        if (!worker) {
            job->error = ENOMEM;
            return false;
        }
        for (unsigned i = 0; i < STACK_WORDS + 2 * GUARD_WORDS; ++i)
            worker->stack[i] = GUARD_VALUE;
        usb_fs_context_init(&worker->context, worker->stack + GUARD_WORDS,
                            STACK_WORDS * sizeof(uint32_t), entry, worker);
    }
    if (worker->broken) {
        job->error = EOVERFLOW;
        return false;
    }
    if (worker->job || worker->running)
        return false;
    job->done = false;
    job->cancelled = progress_cancelled;
    job->error = 0;
    job->pending = true;
    worker->job = job;
    return true;
}
void usb_fs_service(uint32_t budget)
{
    if (!worker || !worker->job || worker->running || worker->broken || !budget)
        return;
    unsigned mask = native_critical_enter();
    if (!guards(worker)) {
        stack_failure();
        native_critical_leave(mask);
        return;
    }
    worker->started = tick();
    worker->budget = budget;
    worker->running = true;
    usb_fs_context_resume(&worker->context);
    worker->running = false;
    if (!guards(worker))
        stack_failure();
    native_critical_leave(mask);
}
void usb_fs_yield(void)
{
    if (worker && worker->running)
        usb_fs_context_yield(&worker->context);
}
bool usb_fs_cancelled(void)
{
    return worker && worker->job && worker->job->cancelled;
}
bool ndvideo_usb_fs_poll(void)
{
    if (!worker || !worker->running)
        return true;
    if ((uint32_t)(tick() - worker->started) >= worker->budget)
        usb_fs_yield();
    return !usb_fs_cancelled();
}
void usb_fs_cancel(UsbFsJob *job)
{
    if (job && job->pending)
        job->cancelled = true;
}
void usb_fs_cancel_active(void)
{
    if (progress_callback)
        progress_cancelled = true;
    if (worker)
        usb_fs_cancel(worker->job);
}
void usb_fs_wait(UsbFsJob *job)
{
    while (job && job->pending && !worker->broken) {
        usb_fs_service(8U);
        uint32_t current = tick();
        if (progress_callback && (uint32_t)(current - progress_last) >= 512U) {
            progress_last = current;
            progress_callback(progress_context);
        }
    }
}
bool usb_fs_idle(void)
{
    return !worker || (!worker->job && !worker->running);
}
size_t usb_fs_memory_bytes(void)
{
    return worker ? sizeof(*worker) : 0;
}
void usb_fs_shutdown(void)
{
    if (!worker)
        return;
    if (worker->job) {
        usb_fs_cancel(worker->job);
        usb_fs_wait(worker->job);
    }
    if (!worker->broken && usb_fs_idle()) {
        usb_fs_context_destroy(&worker->context);
        free(worker);
        worker = NULL;
    }
}
void usb_fs_set_progress(void (*callback)(void *), void *context)
{
    progress_callback = callback;
    progress_context = context;
    progress_last = tick();
    progress_cancelled = false;
}
