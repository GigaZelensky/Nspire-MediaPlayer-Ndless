
#include "usb_storage.h"
#include "usb_controller.h"
#include "usb_fs_worker.h"
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *image;
static bool online = true, parked;
static unsigned generation = 1, ticks, irqmask, reads, polls;
static UsbCommand *owner;
static uint32_t lba;
static unsigned blocks;
static void *target;
static unsigned nodes;
uint32_t usb_controller_ticks(void)
{
    return ++ticks;
}
unsigned native_critical_enter(void)
{
    unsigned old = irqmask;
    irqmask = 0xc0;
    return old;
}
void native_critical_leave(unsigned mask)
{
    irqmask = mask;
}
unsigned native_interrupt_mask(void)
{
    return irqmask;
}
bool usb_controller_online(void)
{
    return online;
}
uint32_t usb_controller_generation(void)
{
    return generation;
}
uint32_t usb_controller_sector_size(void)
{
    return 512;
}
uint32_t usb_controller_last_sector(void)
{
    return 131071;
}
void usb_storage_poll_connection(void) {}
void usb_controller_set_error(const char *t, int e)
{
    (void)t;
    (void)e;
}
bool usb_controller_begin_read(UsbCommand *r, uint32_t s, unsigned n, void *p)
{
    assert(irqmask == 0xc0);
    if (!online || parked || owner)
        return false;
    owner = r;
    r->pending = true;
    r->result = 0;
    lba = s;
    blocks = n;
    target = p;
    polls = 0;
    ++reads;
    return true;
}
int usb_controller_step(UsbCommand *r)
{
    assert(irqmask == 0xc0);
    if (!r->pending)
        return r->result;
    assert(r == owner);
    if (++polls < 3)
        return 0;
    assert(lba < 131072 && blocks <= 131072 - lba);
    if (online)
        memcpy(target, image + (size_t)lba * 512, blocks * 512);
    r->result = online ? 1 : -1;
    r->pending = false;
    owner = NULL;
    return r->result;
}
bool usb_storage_park(void)
{
    parked = true;
    if (owner) {
        usb_controller_step(owner);
        return false;
    }
    return true;
}
void usb_storage_unpark(void)
{
    parked = false;
}
static bool entry(void *unused, const char *name, bool dir, uint32_t bytes)
{
    (void)unused;
    assert(irqmask == 0);
    if (nodes == 0)
        assert(!strcmp(name, "Rick and Morty") && dir && !bytes);
    ++nodes;
    return true;
}
static unsigned char pattern(unsigned i)
{
    return (unsigned char)((i * 73) ^ (i >> 3) ^ 0xa5);
}
static void progress_noop(void *unused)
{
    (void)unused;
}
static bool cancel_directory(void *unused, const char *name, bool directory, uint32_t bytes)
{
    (void)unused;
    (void)name;
    (void)directory;
    (void)bytes;
    usb_fs_cancel_active();
    return true;
}
static void check(const unsigned char *p, unsigned start, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        assert(p[i] == pattern(start + i));
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *in = fopen(argv[1], "rb");
    assert(in);
    image = malloc(131072 * 512);
    assert(image && fread(image, 512, 131072, in) == 131072);
    fclose(in);
    int error = 0;
    assert(usb_volume_mount(&error) && !error);
    assert(irqmask == 0);
    char root[32], path[1024];
    strcpy(root, usb_storage_root());
    assert(usb_directory_list(root, entry, NULL, &error) && nodes == 1);
    usb_fs_set_progress(progress_noop, NULL);
    assert(!usb_directory_list(root, cancel_directory, NULL, &error) && error == ECANCELED);
    usb_fs_set_progress(NULL, NULL);
    snprintf(path, sizeof(path), "%s/../secret.nvp", root);
    assert(!usb_file_open(path, &error) && error == EINVAL);
    snprintf(path, sizeof(path), "%s/Rick and Morty/Season 1/Episode 1.nvp.tns", root);
    UsbFile *f = usb_file_open(path, &error);
    assert(f && usb_file_size(f) == 524288);
    unsigned char buffer[32768];
    assert(usb_file_read_at(f, 123, buffer, sizeof(buffer), &error) == sizeof(buffer));
    check(buffer, 123, sizeof(buffer));
    assert(irqmask == 0);
    UsbRead *r = usb_read_create(f);
    assert(r);
    for (unsigned i = 0; i < 12; ++i) {
        unsigned offset = (i * 7919) % 480000;
        memset(buffer, 0, sizeof(buffer));
        assert(usb_read_begin(r, offset, buffer, sizeof(buffer)));
        unsigned yields = 0;
        while (!usb_read_step(r)) {
            assert(++yields < 100000);
            assert(irqmask == 0);
        }
        assert(!usb_read_error(r) && yields > 0);
        check(buffer, offset, sizeof(buffer));
    }
    /* Cancel with a SCSI command in flight, then seek/read again. */
    assert(usb_read_begin(r, 17, buffer, sizeof(buffer)));
    while (!owner)
        assert(usb_read_step(r) == 0);
    usb_read_cancel(r);
    assert(!owner && usb_read_error(r) == ECANCELED);
    assert(usb_read_begin(r, 777, buffer, sizeof(buffer)));
    while (!usb_read_step(r)) {
    }
    assert(!usb_read_error(r));
    check(buffer, 777, sizeof(buffer));
    /* Background completion followed by another handle must keep its result. */
    assert(usb_read_begin(r, 900, buffer, sizeof(buffer)));
    UsbFile *g = usb_file_open(path, &error);
    assert(g);
    assert(usb_read_step(r) == 1);
    check(buffer, 900, sizeof(buffer));
    usb_file_close(g);
    /* Detach and reattach never let a stale handle read the replacement medium. */
    assert(usb_read_begin(r, 19, buffer, sizeof(buffer)));
    while (!owner)
        assert(usb_read_step(r) == 0);
    online = false;
    ++generation;
    while (!usb_read_step(r)) {
    }
    assert(usb_read_error(r));
    assert(!owner);
    usb_read_destroy(r);
    usb_file_close(f);
    usb_volume_unmount();
    online = true;
    assert(usb_volume_mount(&error));
    f = usb_file_open(path, &error);
    assert(f);
    ++generation;
    assert(usb_file_read_at(f, 0, buffer, 10, &error) < 0 && error == ENODEV);
    usb_file_close(f);
    usb_volume_unmount();
    assert(disk_write(0, buffer, 0, 1) == RES_WRPRT);
    assert(disk_read(0, buffer, UINT64_MAX, 1) == RES_PARERR);
    free(image);
    printf("PASS: nested directories, random seeks, cooperative reads, in-flight cancellation, "
           "concurrent result ownership, detach/generation bounds; %u reads\n",
           reads);
    return 0;
}
