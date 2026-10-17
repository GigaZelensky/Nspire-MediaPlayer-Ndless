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
/* Resume a suspended bus, or start a verified CX II root-only bus whose
 * interrupt is not registered. Never recreate the whole host. */
int native_usb_ui_resume_host(void);
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
void native_usb_ui_bus_observer(void (*)(const char *));
#endif
