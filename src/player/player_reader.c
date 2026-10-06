#include "player_reader.h"
#include "movie_crypto_session.h"
#include "app_task_io.h"
#include "native_interrupts.h"
#include "player_idle.h"
#include "../storage/usb/usb_storage.h"
#include "../storage/usb/usb_controller.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    UsbFile *file;
    UsbRead *read;
    NveReader *crypt;
    uint32_t offset, bytes, started, ended, completed;
    uint32_t cpu_ticks, crypto_ticks, max_step, max_crypto, commands;
    RawPlayerWaitStats wait;
    bool requested, ready, reading;
    int error;
    uint8_t buffer[32768];
} UsbPlayerReader;
struct PlayerReader {
    RawPlayerIo *raw;
    UsbPlayerReader *usb;
};
static UsbPlayerReader *live;
static int create_error;
static bool last_usb;
static struct {
    uint32_t cpu, crypto, max_step, max_crypto, commands;
    int error;
} last_report;

PlayerReader *player_reader_create(const char *path)
{
    create_error = 0;
    PlayerReader *reader = calloc(1, sizeof(*reader));
    if (!reader) {
        create_error = ENOMEM;
        return NULL;
    }
    if (!usb_path(path)) {
        last_usb = false;
        reader->raw = raw_player_create(path);
        if (!reader->raw) {
            create_error = raw_player_error(NULL);
            free(reader);
            return NULL;
        }
        return reader;
    }
    if (live) {
        create_error = EBUSY;
        free(reader);
        return NULL;
    }
    UsbPlayerReader *usb = calloc(1, sizeof(*usb));
    reader->usb = usb;
    if (!usb) {
        create_error = ENOMEM;
        goto failed;
    }
    raw_player_before_read();
    usb->file = usb_file_open(path, &create_error);
    if (!usb->file)
        goto failed;
    usb->read = usb_read_create(usb->file);
    if (!usb->read) {
        create_error = ENOMEM;
        goto failed;
    }
    const NveKeys *keys = movie_crypto_keys(path);
    if (keys) {
        if (keys->header.physical_bytes != usb_file_size(usb->file)) {
            create_error = EIO;
            goto failed;
        }
        usb->crypt = calloc(1, sizeof(*usb->crypt));
        if (!usb->crypt) {
            create_error = ENOMEM;
            goto failed;
        }
        nve_reader_init(usb->crypt, keys);
    }
    live = usb;
    last_usb = true;
    return reader;
failed:
    player_reader_destroy(reader);
    return NULL;
}
void player_reader_cancel(PlayerReader *reader)
{
    if (!reader)
        return;
    if (reader->raw) {
        raw_player_cancel(reader->raw);
        return;
    }
    UsbPlayerReader *usb = reader->usb;
    if (!usb)
        return;
    usb_read_cancel(usb->read);
    if (usb->crypt)
        nve_reader_cancel(usb->crypt);
    usb->requested = usb->ready = usb->reading = false;
    if (usb->crypt)
        nve_wipe(usb->buffer, sizeof(usb->buffer));
}
void player_reader_destroy(PlayerReader *reader)
{
    if (!reader)
        return;
    if (reader->raw)
        raw_player_destroy(reader->raw);
    UsbPlayerReader *usb = reader->usb;
    if (usb) {
        last_report.cpu = usb->cpu_ticks;
        last_report.crypto = usb->crypto_ticks;
        last_report.max_step = usb->max_step;
        last_report.max_crypto = usb->max_crypto;
        last_report.commands = usb->commands;
        last_report.error = usb->error;
        player_reader_cancel(reader);
        usb_read_destroy(usb->read);
        usb_file_close(usb->file);
        if (usb->crypt) {
            nve_wipe(usb->crypt, sizeof(*usb->crypt));
            free(usb->crypt);
        }
        if (live == usb)
            live = NULL;
        nve_wipe(usb, sizeof(*usb));
        free(usb);
    }
    free(reader);
}
static void complete(UsbPlayerReader *usb)
{
    usb->ready = true;
    usb->ended = 0U - usb_controller_ticks();
    usb->completed = usb->bytes;
}
static void advance_crypto(UsbPlayerReader *usb)
{
    uint32_t start = usb_controller_ticks();
    nve_reader_step(usb->crypt);
    uint32_t cost = usb_controller_ticks() - start;
    usb->crypto_ticks += cost;
    if (cost > usb->max_crypto)
        usb->max_crypto = cost;
    if (usb->crypt->phase == NVE_READ_DONE)
        complete(usb);
    else if (usb->crypt->phase == NVE_READ_ERROR)
        usb->error = EIO;
}
static void step(UsbPlayerReader *usb)
{
    if (!usb || !usb->requested || usb->ready || usb->error)
        return;
    if (usb_controller_parked())
        return;
    if (usb->reading) {
        int result = usb_read_step(usb->read);
        if (!result)
            return;
        usb->reading = false;
        if (result < 0) {
            usb->error = usb_read_error(usb->read);
            return;
        }
        if (usb->crypt)
            nve_reader_supplied(usb->crypt, true);
        else
            complete(usb);
        return;
    }
    if (usb->crypt && usb->crypt->phase != NVE_READ_FETCH) {
        advance_crypto(usb);
        return;
    }
    uint32_t offset = usb->crypt ? nve_reader_physical_offset(usb->crypt) : usb->offset;
    uint32_t bytes = usb->crypt ? usb->crypt->unit_bytes + NVE_TAG_BYTES : usb->bytes;
    void *target = usb->crypt ? usb->crypt->block : usb->buffer;
    if (usb_read_begin(usb->read, offset, target, bytes)) {
        usb->reading = true;
        ++usb->commands;
    } else if (usb_read_error(usb->read))
        usb->error = usb_read_error(usb->read);
}
void player_reader_service(uint32_t budget)
{
    if (!live) {
        raw_player_service(budget);
        return;
    }
    if (!budget || usb_controller_parked())
        return;
    uint32_t start = usb_controller_ticks();
    unsigned mask = native_critical_enter();
    do {
        uint32_t before = usb_controller_ticks();
        step(live);
        uint32_t cost = usb_controller_ticks() - before;
        live->cpu_ticks += cost;
        if (cost > live->max_step)
            live->max_step = cost;
        if (!live->requested || live->ready || live->error)
            break;
    } while ((uint32_t)(usb_controller_ticks() - start) < budget);
    native_critical_leave(mask);
}
bool player_reader_crypto_step(uint32_t spare)
{
    if (!live)
        return raw_player_crypto_step(spare);
    if (!live->crypt || !live->requested || live->ready || live->reading || live->error ||
        (live->crypt->phase != NVE_READ_VERIFY && live->crypt->phase != NVE_READ_DECRYPT &&
         live->crypt->phase != NVE_READ_COPY) ||
        (uint64_t)spare <= 33U + (live->max_crypto ? 2ULL * live->max_crypto + 1 : 33U))
        return false;
    advance_crypto(live);
    return true;
}
bool player_reader_read_step(uint32_t spare)
{
    if (!live)
        return raw_player_read_step(spare);
    if (!live->requested || live->ready || live->error || usb_controller_parked() ||
        (uint64_t)spare <= 33U + (live->max_step ? 2ULL * live->max_step + 1 : 33U))
        return false;
    player_reader_service(1);
    return true;
}
int player_reader_read(PlayerReader *reader, uint64_t offset, void *destination, size_t bytes,
                       bool wait)
{
    if (!reader)
        return -1;
    if (reader->raw)
        return raw_player_read(reader->raw, offset, destination, bytes, wait);
    UsbPlayerReader *usb = reader->usb;
    uint32_t length = player_reader_file_bytes(reader);
    if (!usb || usb->error || !destination || !bytes || bytes > sizeof(usb->buffer) ||
        offset > length || bytes > length - offset)
        return -1;
    if (!usb->requested || usb->offset != offset || usb->bytes != bytes) {
        player_reader_cancel(reader);
        usb->offset = (uint32_t)offset;
        usb->bytes = (uint32_t)bytes;
        usb->requested = true;
        usb->started = 0U - usb_controller_ticks();
        if (usb->crypt && !nve_reader_begin(usb->crypt, usb->offset, usb->buffer, usb->bytes)) {
            usb->error = EINVAL;
            return -1;
        }
    }
    uint32_t start = usb_controller_ticks(), cpu = usb->cpu_ticks, crypto = usb->crypto_ticks;
    do {
        if (wait)
            player_reader_service(8);
        if (usb->error)
            break;
        if (usb->ready) {
            memcpy(destination, usb->buffer, bytes);
            usb->requested = usb->ready = false;
            break;
        }
        if (wait)
            app_task_io_service(8);
    } while (wait);
    if (wait)
        usb->wait = (RawPlayerWaitStats){.wall_ticks = usb_controller_ticks() - start,
                                         .reader_ticks = usb->cpu_ticks - cpu,
                                         .crypto_ticks = usb->crypto_ticks - crypto};
    return usb->error ? -1 : usb->requested ? 0 : 1;
}
bool player_reader_idle(unsigned milliseconds)
{
    if (!live)
        return raw_player_idle(milliseconds);
    uint32_t start = usb_controller_ticks(),
             duration = (uint32_t)(((uint64_t)milliseconds * 32768U + 999) / 1000U);
    do {
        app_task_io_service(8);
        player_reader_service(8);
        uint32_t elapsed = usb_controller_ticks() - start;
        if (elapsed >= duration)
            break;
        if (!app_task_io_pending() && (!live->requested || live->ready || live->error)) {
            unsigned sleep = (unsigned)((uint64_t)(duration - elapsed) * 1000U / 32768U);
            if (sleep)
                player_idle_sleep(sleep);
            else
                break;
        }
    } while ((uint32_t)(usb_controller_ticks() - start) < duration);
    return true;
}
bool player_reader_needs_request(const PlayerReader *r)
{
    return r && (r->raw ? raw_player_needs_request(r->raw) : !r->usb->requested || r->usb->ready);
}
void player_reader_after_clock_reset(PlayerReader *r)
{
    if (!r)
        return;
    if (r->raw)
        raw_player_after_clock_reset(r->raw);
    else {
        player_reader_cancel(r);
        usb_storage_unpark();
    }
}
int player_reader_error(const PlayerReader *r)
{
    return r ? (r->raw ? raw_player_error(r->raw) : r->usb->error) : create_error;
}
uint32_t player_reader_file_bytes(const PlayerReader *r)
{
    return !r              ? 0
           : r->raw        ? raw_player_file_bytes(r->raw)
           : r->usb->crypt ? r->usb->crypt->keys->header.plain_bytes
                           : usb_file_size(r->usb->file);
}
size_t player_reader_memory_bytes(void)
{
    return sizeof(PlayerReader) + usb_storage_memory_bytes() +
           (live ? sizeof(*live) + usb_file_memory(live->file) +
                       (live->crypt ? sizeof(*live->crypt) : 0)
                 : raw_player_memory_bytes());
}
bool player_reader_last_read(PlayerReader *r, uint32_t *start, uint32_t *end, uint32_t *bytes)
{
    if (!r)
        return false;
    if (r->raw)
        return raw_player_last_read(r->raw, start, end, bytes);
    if (!r->usb->completed)
        return false;
    *start = r->usb->started;
    *end = r->usb->ended;
    *bytes = r->usb->completed;
    return true;
}
bool player_reader_last_wait(const PlayerReader *r, RawPlayerWaitStats *out)
{
    if (!r || !out)
        return false;
    if (r->raw)
        return raw_player_last_wait(r->raw, out);
    *out = r->usb->wait;
    return out->wall_ticks != 0;
}
void player_reader_debug(FILE *file, const PlayerReader *r)
{
    if ((r && r->usb) || (!r && last_usb)) {
        const UsbPlayerReader *u = r ? r->usb : NULL;
        usb_storage_debug(file);
        fprintf(file,
                "usb_reader_work cpu_ticks=%lu crypto_ticks=%lu max_step_ticks=%lu "
                "max_crypto_ticks=%lu requests=%lu error=%d\n",
                (unsigned long)(u ? u->cpu_ticks : last_report.cpu),
                (unsigned long)(u ? u->crypto_ticks : last_report.crypto),
                (unsigned long)(u ? u->max_step : last_report.max_step),
                (unsigned long)(u ? u->max_crypto : last_report.max_crypto),
                (unsigned long)(u ? u->commands : last_report.commands),
                u ? u->error : last_report.error);
    } else
        raw_player_debug(file, r ? r->raw : NULL);
}
