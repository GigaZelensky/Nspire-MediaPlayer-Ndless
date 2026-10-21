#ifndef NDVIDEO_NATIVE_USB_UI_H
#define NDVIDEO_NATIVE_USB_UI_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "storage/usb/usb_route.h"
/* Call with genuine native SRAM and IRQ/FIQ masked. The reservation remains
 * across connection windows so the OS cannot redraw over the player. */
int native_usb_ui_acquire(bool (*)(void *), void *);
int native_usb_ui_reassert(void);
/* One OS tick with the native mapping and Nucleus interrupt policy enabled. */
int native_usb_ui_wait(void);
/* Bounded IRQ span; no OS sleep/yield call. */
int native_usb_ui_poll(void);
/* Resume an existing bus, or request its native OTG owner's power/reset/start
 * sequence for a verified cold CX II root. A queued request is not readiness;
 * callers recheck current native/MMIO state before borrowing the bus. */
int native_usb_ui_resume_host(void);
enum { NATIVE_USB_SESSION_PENDING = -70 };
/* CPU-only intent, coalesced by the native owner at a quiescent root.
 * Does not access the OS mapping or perform hardware I/O. */
void native_usb_ui_request_session_restart(void);
/* Read-only candidate discovery. The controller authenticates its own
 * resident callbacks before reusing a binding from a previous app launch. */
bool native_usb_ui_storage_binding(uint32_t *owner, uint32_t *device, uint32_t *interface);
bool native_usb_ui_storage_binding_after(unsigned after,uint32_t *owner,uint32_t *device,uint32_t *interface);
/* Background preparation only: no display/input ownership, sleep or native
 * function invocation. Must run under the genuine native SRAM mapping. */
int native_usb_ui_prepare_step(void);
/* Resolve without taking UI ownership. Progress keeps the connection window
 * responsive before the OS timer is handed back for native calls. */
bool native_usb_ui_clock(const volatile uint32_t **counter, uint32_t *hz,
                         bool (*progress)(void *), void *context);
int native_usb_ui_release(void);
bool native_usb_ui_active(void);
void native_usb_ui_fast_enumeration_begin(void);
bool native_usb_ui_fast_enumeration_end(void);
const char *native_usb_ui_stage(void);
void native_usb_ui_debug(FILE *);
int native_usb_ui_resume_host_lease(void);
int native_usb_ui_bus_acquire(void);
void native_usb_ui_bus_release(void);
bool native_usb_ui_bus_is_leased(void);
unsigned native_usb_ui_bus_lock_stage(void);
uint32_t native_usb_ui_bus_irq_mask(void);
bool native_usb_ui_bus_route(void *,uint8_t,UsbDeviceRoute *);
bool native_usb_ui_bus_device_state(void *, uint8_t address, int interface_number,
                                    UsbDeviceState *);
void native_usb_ui_bus_snapshot(uint32_t [8]);
/* Read-only CX II native queue/interrupt details for setup-failure reports.
 * CPU-view queue fields are sampled without changing cache or controller state. */
void native_usb_ui_bus_details(uint32_t [16]);
/* Read-only CX II role/PHY state, including the transceiver suspend control. */
void native_usb_ui_bus_phy(uint32_t [10]);
void native_usb_ui_bus_observer(void (*)(const char *));
#endif
