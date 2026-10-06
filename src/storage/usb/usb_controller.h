#ifndef NDVIDEO_USB_CONTROLLER_H
#define NDVIDEO_USB_CONTROLLER_H
#include "usb_scsi.h"
#include <stdio.h>
/* Private bridge between the filesystem and the platform controller. */
bool usb_controller_online(void);
uint32_t usb_controller_generation(void);
uint32_t usb_controller_sector_size(void);
uint32_t usb_controller_last_sector(void);
typedef struct {
    bool pending;
    int result;
} UsbCommand;
bool usb_controller_begin_read(UsbCommand *, uint32_t, unsigned, void *);
int usb_controller_step(UsbCommand *);
int usb_controller_error(void);
bool usb_controller_parked(void);
uint32_t usb_controller_ticks(void);
int usb_controller_read_sync(uint32_t, unsigned, void *);
void usb_volume_unmount(void);
bool usb_volume_mount(int *);
void usb_controller_set_error(const char *, int);
void usb_controller_debug(FILE *);
#endif
