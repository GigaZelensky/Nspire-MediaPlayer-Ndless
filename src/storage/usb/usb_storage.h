#ifndef NDVIDEO_USB_STORAGE_H
#define NDVIDEO_USB_STORAGE_H
#include "../../media_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct UsbFile UsbFile;
typedef struct UsbRead UsbRead;
typedef struct UsbDirectory UsbDirectory;
typedef bool (*UsbDirectoryEntry)(void *, const char *, bool, uint64_t);
bool usb_path(const char *);
bool usb_storage_connected(void);
bool usb_storage_mounted(void);
uint32_t usb_storage_generation(void);
const char *usb_storage_root(void);
const char *usb_storage_error(void);
const char *usb_storage_stage(void);
void usb_storage_debug(FILE *);
size_t usb_storage_memory_bytes(void);
/* Called only at an explicit native-OS connection boundary. */
bool usb_storage_supported(void);
bool usb_storage_prepare_connection(void);
bool usb_storage_attachment_ready(void);
/* Candidate selection/endpoint setup require native clock/mapping. The final
 * SCSI readiness/capacity probe requires the player's polled-transfer timer. */
bool usb_storage_next_candidate(void);
bool usb_storage_prepare_endpoints(void);
bool usb_storage_driver_registered(void);
bool usb_storage_finish_connection(void);
void usb_storage_stop_accepting(void);
void usb_storage_watch_attachments(void);
bool usb_storage_shutdown(void);
void usb_storage_poll_connection(void);
/* Park only at a complete SCSI-command boundary before enabling OS services. */
bool usb_storage_park(void);
void usb_storage_unpark(void);
bool usb_storage_quiescent(void);

bool usb_directory_list(const char *, UsbDirectoryEntry, void *, int *);
/* Incremental foreground iterator: 0 pending, 1 entry, 2 end, -1 error.
 * The name remains valid until the next step or close. */
UsbDirectory *usb_directory_open(const char *, int *);
int usb_directory_next(UsbDirectory *, const char **, bool *, int *);
void usb_directory_close(UsbDirectory *);
UsbFile *usb_file_open(const char *, int *);
uint64_t usb_file_size(const UsbFile *);
size_t usb_file_memory(const UsbFile *);
void usb_file_close(UsbFile *);
int usb_file_read_at(UsbFile *, uint64_t, void *, uint32_t, int *);
UsbRead *usb_read_create(UsbFile *);
void usb_read_destroy(UsbRead *);
bool usb_read_begin(UsbRead *, uint64_t, void *, uint32_t);
int usb_read_step(UsbRead *);
void usb_read_cancel(UsbRead *);
int usb_read_error(const UsbRead *);
#endif
