#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct { bool online; unsigned speed; } UsbController;
typedef struct { void *usb; void *raw; } PlayerReader;
typedef struct { void *async_io; } Movie;
typedef struct MovieAsyncIo { PlayerReader *raw; } MovieAsyncIo;
static UsbController *controller;

/* PRODUCTION */

int main(void)
{
    UsbController device = {true, 2};
    PlayerReader reader = {&device, NULL}, nand = {NULL, &device};
    MovieAsyncIo state = {&reader};
    Movie movie = {&state};
    assert(!usb_controller_link_speed());
    assert(!movie_async_full_speed_usb(NULL));
    assert(!player_reader_full_speed_usb(NULL));
    assert(!movie_async_full_speed_usb(&movie));
    controller = &device;
    assert(usb_controller_link_speed() == 2);
    assert(movie_async_full_speed_usb(&movie));
    assert(!player_reader_full_speed_usb(&nand));
    device.speed = 3;
    assert(!movie_async_full_speed_usb(&movie));
    device.speed = 2;
    device.online = false;
    assert(!usb_controller_link_speed());
    assert(!movie_async_full_speed_usb(&movie));
    movie.async_io = NULL;
    assert(!movie_async_full_speed_usb(&movie));
    puts("PASS full-speed reader selection: USB/NAND, online/offline, high/full speed, null state");
    return 0;
}
