#ifndef NDVIDEO_USB_AUDIO_H
#define NDVIDEO_USB_AUDIO_H
#include "audio_buffer.h"
#include "usb_audio_format.h"
#include "storage/usb/usb_route.h"
#include "input.h"

typedef struct UsbAudio UsbAudio;
typedef UsbDeviceRoute UsbAudioRoute;
void usb_audio_set_route_lookup(bool (*)(void *, uint8_t, UsbAudioRoute *), void *);
void usb_audio_set_state_lookup(bool (*)(void *, uint8_t, int, UsbDeviceState *), void *);
void usb_audio_set_progress(bool (*)(void *), void *);
void usb_audio_set_stage_callback(void (*)(const char *));
void usb_audio_prefer_rate(uint32_t);
void usb_audio_probe_only(bool enabled);
void usb_audio_topology_debug(FILE *);
void usb_audio_hub_debug(FILE *);
/* CX II FOTG210, direct full/high-speed UAC1/UAC2 device enumerated by TI-OS.
 * No device reset, address change, resident driver, or OS-specific pointers. */
UsbAudio *usb_audio_open(uint32_t (*now_ms)(void), const char **status);
bool usb_audio_configure(UsbAudio *, const char **);
bool usb_audio_close(UsbAudio *u);
bool usb_audio_pause(UsbAudio *u);
bool usb_audio_quiesce(UsbAudio *);
bool usb_audio_start(UsbAudio *u, AudioBuffer *s, uint64_t position, unsigned rate_num,
                     unsigned rate_den, unsigned volume);
bool usb_audio_service(UsbAudio *u);
/* Foreground only, after usb_storage_quiescent(): a bounded read-only device
 * reply check. Never call from the storage periodic-service callback. */
bool usb_audio_check_health(UsbAudio *u);
uint64_t usb_audio_position(UsbAudio *u);
void usb_audio_volume(UsbAudio *u, unsigned volume);
const char *usb_audio_error(const UsbAudio *u);
/* Last transfer failure remains available after the USB owner is closed. */
const char *usb_audio_transfer_fault(void);
/* Passive snapshot at close; no control request and no per-frame logging. */
void usb_audio_capture_stream(UsbAudio *);
const char *usb_audio_last_stream(void);
const char *usb_audio_last_control(void);
void usb_audio_describe(const UsbAudio *u, char *text, size_t capacity);
uint32_t usb_audio_output_key(const UsbAudio *);
#endif
