#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include "usb_fs_worker.h"
static void (*start_function)(void *);
static void *start_argument;
static VOID CALLBACK trampoline(void *unused)
{
    (void)unused;
    start_function(start_argument);
    abort();
}
void usb_fs_context_init(UsbFsContext *c, void *stack, size_t bytes, void (*entry)(void *),
                         void *argument)
{
    (void)stack;
    (void)bytes;
    if (!IsThreadAFiber())
        assert(ConvertThreadToFiber(NULL));
    c->foreground = GetCurrentFiber();
    start_function = entry;
    start_argument = argument;
    c->background = CreateFiber(1024 * 1024, trampoline, NULL);
    assert(c->background);
}
void usb_fs_context_resume(UsbFsContext *c)
{
    SwitchToFiber(c->background);
}
void usb_fs_context_yield(UsbFsContext *c)
{
    SwitchToFiber(c->foreground);
}
void usb_fs_context_destroy(UsbFsContext *c)
{
    DeleteFiber(c->background);
    c->background = NULL;
}

#else
#include <ucontext.h>
#include <assert.h>
#include <stdlib.h>
#include "usb_fs_worker.h"
static void (*start_function)(void *);
static void *start_argument;
static void trampoline(void)
{
    start_function(start_argument);
    abort();
}
void usb_fs_context_init(UsbFsContext *c, void *stack, size_t bytes, void (*entry)(void *),
                         void *argument)
{
    ucontext_t *foreground = calloc(1, sizeof(*foreground)),
               *background = calloc(1, sizeof(*background));
    assert(foreground && background);
    assert(!getcontext(background));
    background->uc_stack.ss_sp = stack;
    background->uc_stack.ss_size = bytes;
    background->uc_link = foreground;
    start_function = entry;
    start_argument = argument;
    makecontext(background, trampoline, 0);
    c->foreground = foreground;
    c->background = background;
}
void usb_fs_context_resume(UsbFsContext *c)
{
    assert(!swapcontext(c->foreground, c->background));
}
void usb_fs_context_yield(UsbFsContext *c)
{
    assert(!swapcontext(c->background, c->foreground));
}
void usb_fs_context_destroy(UsbFsContext *c)
{
    free(c->foreground);
    free(c->background);
    c->foreground = c->background = NULL;
}
#endif
