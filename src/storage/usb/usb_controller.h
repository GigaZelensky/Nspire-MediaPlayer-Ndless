#ifndef NDVIDEO_USB_CONTROLLER_H
#define NDVIDEO_USB_CONTROLLER_H
#include "usb_scsi.h"
#include <stdio.h>
/* Private bridge between the filesystem and the platform controller. */
bool usb_controller_online(void);
/* Verified single-device audio attachment; hubs keep storage discovery open. */
bool usb_controller_audio_only(void);
/* Runs bounded periodic work while a bulk request waits. It must never issue
 * storage requests or call native services. Clear before destroying its owner. */
void usb_controller_set_periodic_service(void (*)(void *),void *);
unsigned usb_controller_link_speed(void);
uint32_t usb_controller_generation(void);
uint32_t usb_controller_sector_size(void);
unsigned usb_controller_max_read_bytes(void);
uint64_t usb_controller_last_sector(void);
typedef struct {
    bool pending;
    int result;
} UsbCommand;
bool usb_controller_begin_read(UsbCommand *, uint64_t, unsigned, void *);
int usb_controller_step(UsbCommand *);
int usb_controller_error(void);
bool usb_controller_parked(void);
uint32_t usb_controller_ticks(void);
int usb_controller_read_sync(uint64_t, unsigned, void *);
void usb_volume_unmount(void);
bool usb_volume_mount(int *);
void usb_controller_set_error(const char *, int);
void usb_controller_debug(FILE *);
#endif
