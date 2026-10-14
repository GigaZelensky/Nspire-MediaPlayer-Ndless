#include "usb_controller.h"
#include "usb_scsi_commands.h"
#include "usb_registers.h"
#include "usb_storage.h"
#include "../../platform/hardware_acceleration.h"
#include "usb_fs_worker.h"
#include "../../platform/arm926_ram_span.h"
#include "../../platform/arm926_cpu_ram_span.h"
#include "../../player/native_interrupts.h"
#include "../../player/native_usb_ui.h"
#include <os.h>
#include <usb.h>
#include <usbdi.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define USB_DMA_ALLOCATION_BYTES (USB_READ_BYTES + 96U + 4095U)
typedef char QueueHeadSize[(sizeof(UsbQueueHead) == 64) ? 1 : -1];
typedef char DescriptorSize[(sizeof(UsbQtd) == 32) ? 1 : -1];
typedef struct UsbController UsbController;
typedef struct {
    int (*callbacks[4])(device_t, UsbController *);
    UsbController *host;
    void (*native_free)(void *);
    void *allocation;
    volatile uint32_t active_callbacks, retired;
    void *methods[7];
    char name[16];
} UsbCallbackStub;
typedef char StubHostOffset[(offsetof(UsbCallbackStub, host) == 16) ? 1 : -1];
typedef char StubFreeOffset[(offsetof(UsbCallbackStub, native_free) == 20) ? 1 : -1];
typedef char StubAllocationOffset[(offsetof(UsbCallbackStub, allocation) == 24) ? 1 : -1];
typedef char StubActiveOffset[(offsetof(UsbCallbackStub, active_callbacks) == 28) ? 1 : -1];
typedef char StubRetiredOffset[(offsetof(UsbCallbackStub, retired) == 32) ? 1 : -1];
struct UsbController {
    UsbCallbackStub *stub;
    UsbRegisters registers;
    UsbTransport transport;
    UsbScsi scsi;
    void *arena;
    UsbCommand *owner;
    usbd_device_handle device;
    usbd_interface_handle interface;
    usbd_pipe_handle input, output;
    device_t native_owner;
    usbd_status (*abort_pipe)(usbd_pipe_handle);
    usbd_status (*close_pipe)(usbd_pipe_handle);
    unsigned ivars_offset, descriptor_offset;
    uint32_t sector_size, generation;
    uint64_t last_sector;
    uint32_t matches, attaches, detaches, cleanups, commands, read_bytes, argument_aliases;
    uint32_t max_step_ticks, cpu_ticks;
    /* Last four enumeration decisions, never sampled in the playback path. */
    uint32_t match_trace[4][6];
    uint8_t address, speed, endpoint_in, endpoint_out;
    bool registered, accepting, attached, online, parked, init_failed, reused_driver;
    int error;
};
unsigned usb_controller_max_read_bytes(void){return hardware_acceleration_enabled(HW_ACCEL_USB_LARGE)?USB_READ_BYTES:4096U;}
static UsbController *controller;

unsigned usb_controller_link_speed(void)
{
    return controller && controller->online ? controller->speed : 0U;
}
typedef char StubOwnerOffset[(offsetof(UsbController, stub) == 0) ? 1 : -1];
static uint32_t next_generation;
static char error_text[128];
static int last_error = ENODEV;
static const char *operation = "idle";
extern const uint8_t usb_callback_start[], usb_callback_end[], usb_callback_context[];
extern const uint8_t usb_callback_match[], usb_callback_attach[], usb_callback_detach[],
    usb_callback_cleanup[];

static uint32_t word(uint32_t at, void *unused)
{
    (void)unused;
    return REG(at);
}
static bool ram(const void *pointer, unsigned bytes)
{
    uint32_t at = (uint32_t)(uintptr_t)pointer, table, control, domain;
    if (!bytes || at < 0x10000000U || bytes > 0x4000000U || at > 0x14000000U - bytes)
        return false;
    bytes += at & 3U;
    at &= ~3U;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(control));
    __asm__ volatile("mrc p15,0,%0,c2,c0,0" : "=r"(table));
    __asm__ volatile("mrc p15,0,%0,c3,c0,0" : "=r"(domain));
    table &= 0xffffc000U;
    if (!(control & 1) || ((domain & 3) != 1 && (domain & 3) != 3) ||
        !((table >= 0x10000000U && table <= 0x13ffc000U) ||
          (table >= 0xa4000000U && table <= 0xa400c000U)))
        return false;
    uint32_t self = REG(table + (table >> 20) * 4U);
    if ((self & 0x1e3U) != 2U || !(self & 0xc00U) || (self & 0xfff00000U) != (table & 0xfff00000U))
        return false;
    return arm926_ram_span(at, bytes, table, word, NULL, NULL);
}
static bool argument_ram(UsbController *c,const void *pointer,unsigned bytes)
{
    if(ram(pointer,bytes))return true;
    uint32_t at=(uint32_t)(uintptr_t)pointer,control,table,domain;
    if(!bytes||bytes>0x4000000U||at>UINT32_MAX-bytes-(at&3U))return false;
    bytes+=at&3U;at&=~3U;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(control));
    __asm__ volatile("mrc p15,0,%0,c2,c0,0":"=r"(table));
    __asm__ volatile("mrc p15,0,%0,c3,c0,0":"=r"(domain));table&=0xffffc000U;
    if(!(control&1U)||((domain&3U)!=1U&&(domain&3U)!=3U)||
       !((table>=0x10000000U&&table<=0x13ffc000U)||(table>=0xa4000000U&&table<=0xa400c000U)))return false;
    uint32_t self=REG(table+(table>>20)*4U);
    if((self&0x1e3U)!=2U||!(self&0xc00U)||(self&0xfff00000U)!=(table&0xfff00000U))return false;
    bool valid=arm926_cpu_ram_span(at,bytes,table,word,NULL);
    if(valid)++c->argument_aliases;
    return valid;
}
#define EXPORT(name, number)                                                                       \
    static uint32_t export_##name(void)                                                            \
    {                                                                                              \
        unsigned mask = native_interrupt_mask();                                                   \
        register uint32_t v __asm__("r0");                                                         \
        __asm__ volatile("swi %[n]"                                                                \
                         : "=r"(v)                                                                 \
                         : [n] "i"(__SYSCALLS_ISVAR | (number))                                    \
                         : "r1", "r2", "r3", "r4", "r12", "lr", "memory", "cc");                   \
        uint32_t result = v;                                                                       \
        native_critical_leave(mask);                                                               \
        return result;                                                                             \
    }
EXPORT(ivars, e_device_get_ivars)
EXPORT(descriptor, e_usbd_get_interface_descriptor)
EXPORT(abort, e_usbd_abort_pipe)
EXPORT(close, e_usbd_close_pipe)
EXPORT(free, e_free)
EXPORT(alloc, e_malloc)
static bool getter(uint32_t at, unsigned *offset)
{
    if ((at & 3) || !ram((void *)(uintptr_t)at, 8))
        return false;
    uint32_t a = REG(at), b = REG(at + 4);
    if ((a & 0xfffff000U) != 0xe5900000U || (a & 3) || (a & 0xfff) > 64 ||
        (b != 0xe12fff1eU && b != 0xe1a0f00eU))
        return false;
    *offset = a & 0xfff;
    return true;
}
static struct usb_attach_arg *arguments(UsbController *c, device_t d)
{
    if (((uintptr_t)d & 3) || !ram(d, c->ivars_offset + 4))
        return NULL;
    struct usb_attach_arg *a = *(struct usb_attach_arg **)((uint8_t *)d + c->ivars_offset);
    return a && !((uintptr_t)a & 3) && argument_ram(c,a,32) ? a : NULL;
}
static bool mass_storage(UsbController *c, struct usb_attach_arg *a)
{
    if (!a || !a->iface || ((uintptr_t)a->iface & 3) || !ram(a->iface, c->descriptor_offset + 4))
        return false;
    const usb_interface_descriptor_t *d =
        *(const usb_interface_descriptor_t **)((uint8_t *)a->iface + c->descriptor_offset);
    return d && ram(d, sizeof(*d)) && d->bLength >= sizeof(*d) &&
           d->bDescriptorType == UDESC_INTERFACE && d->bInterfaceClass == UICLASS_MASS &&
           d->bInterfaceSubClass == UISUBCLASS_SCSI && d->bInterfaceProtocol == UIPROTO_MASS_BBB;
}
static int match(device_t d, UsbController *c)
{
    unsigned mask = native_critical_enter();
    uint32_t *trace = c->match_trace[c->matches % 4];
    memset(trace, 0, sizeof(c->match_trace[0]));
    trace[0] = (uint32_t)(uintptr_t)d;
    ++c->matches;
    unsigned reason = 1;
    int result = UMATCH_NONE;
    if (!c->accepting || c->attached) goto done;
    reason = 2;
    if (((uintptr_t)d & 3U) || !ram(d, c->ivars_offset + 4)) goto done;
    trace[1] = *(uint32_t *)((uint8_t *)d + c->ivars_offset);
    struct usb_attach_arg *a = arguments(c, d);
    reason = 3;
    if (!a) goto done;
    trace[2] = (uint32_t)(uintptr_t)a->iface;
    reason = 4;
    if (!a->iface) goto done;
    reason = 5;
    if (((uintptr_t)a->iface & 3U) || !ram(a->iface, c->descriptor_offset + 4)) goto done;
    const usb_interface_descriptor_t *descriptor =
        *(const usb_interface_descriptor_t **)((uint8_t *)a->iface + c->descriptor_offset);
    trace[3] = (uint32_t)(uintptr_t)descriptor;
    reason = 6;
    if (!descriptor || !ram(descriptor, sizeof(*descriptor))) goto done;
    trace[5] = (uint32_t)descriptor->bInterfaceClass << 16 |
               (uint32_t)descriptor->bInterfaceSubClass << 8 | descriptor->bInterfaceProtocol;
    reason = 7;
    if (mass_storage(c, a)) { result = UMATCH_IFACECLASS; reason = 0; }
done:
    trace[4] = reason;
    native_critical_leave(mask);
    return result;
}
static int attach(device_t d, UsbController *c)
{
    unsigned mask = native_critical_enter();
    ++c->attaches;
    int result = ENXIO;
    struct usb_attach_arg *a = arguments(c, d);
    if (c->accepting && !c->attached && mass_storage(c, a) && ram(a->device, 12)) {
        c->device = a->device;
        c->interface = a->iface;
        c->address = ((const uint8_t *)a->device)[8];
        c->speed = ((const uint8_t *)a->device)[11];
        c->native_owner = d;
        c->attached = true;
        c->online = false;
        c->error = 0;
        c->generation = ++next_generation;
        c->sector_size = c->last_sector = 0;
        c->endpoint_in = c->endpoint_out = 0;
        c->init_failed = false;
        c->transport.toggle_in = c->transport.toggle_out = 0;
        result = 0;
    }
    native_critical_leave(mask);
    return result;
}
static void close_pipes(UsbController *c)
{
    usbd_pipe_handle input = c->input, output = c->output;
    c->input = c->output = NULL;
    /* Native pipes contain no OS transfer requests: all payload DMA uses
     * our separate queue.
     * Consume ownership before calling native cleanup. */
    if (input) {
        c->abort_pipe(input);
        c->close_pipe(input);
    }
    if (output) {
        c->abort_pipe(output);
        c->close_pipe(output);
    }
}
static int detach(device_t d, UsbController *c)
{
    unsigned mask = native_critical_enter();
    ++c->detaches;
    if (d == c->native_owner) {
        c->attached = c->online = false;
        c->error = ENODEV;
        c->generation = ++next_generation;
        if (usb_transport_quiescent(&c->transport))
            close_pipes(c);
        c->device = NULL;
        c->interface = NULL;
        c->native_owner = NULL;
    }
    native_critical_leave(mask);
    return 0;
}
static int cleanup(device_t unused, UsbController *c)
{
    (void)unused;
    unsigned mask = native_critical_enter();
    ++c->cleanups;
    c->registered = c->accepting = c->attached = c->online = false;
    c->generation = ++next_generation;
    c->error = ENODEV;
    if (usb_transport_quiescent(&c->transport))
        close_pipes(c);
    c->device = NULL;
    c->interface = NULL;
    c->native_owner = NULL;
    native_critical_leave(mask);
    return 0;
}
static uint32_t bus_read(void *context, unsigned offset)
{
    return REG(usb_register_address(context, offset));
}
static void bus_write(void *context, unsigned offset, uint32_t value)
{
    REG(usb_register_address(context, offset)) = value;
}
static uint32_t ticks(void *context)
{
    (void)context;
    return 0U - REG(0x900c0004U);
}
uint32_t usb_controller_ticks(void)
{
    return ticks(NULL);
}
static void drain(void *context)
{
    (void)context;
    __asm__ volatile("mcr p15,0,%0,c7,c10,4" ::"r"(0) : "memory");
}
static void clean(void *context, void *p, size_t n)
{
    for (uintptr_t at = (uintptr_t)p & ~(uintptr_t)31; at < (uintptr_t)p + n; at += 32)
        __asm__ volatile("mcr p15,0,%0,c7,c14,1" ::"r"(at) : "memory");
    drain(context);
}
static void invalidate(void *context, void *p, size_t n)
{
    (void)context;
    for (uintptr_t at = (uintptr_t)p & ~(uintptr_t)31; at < (uintptr_t)p + n; at += 32)
        __asm__ volatile("mcr p15,0,%0,c7,c6,1" ::"r"(at) : "memory");
}
void usb_controller_set_error(const char *text, int error)
{
    last_error = error ? error : EIO;
    const char *message = text ? text : "USB operation failed.";
    size_t bytes = strlen(message);
    if (bytes >= sizeof(error_text))
        bytes = sizeof(error_text) - 1;
    memcpy(error_text, message, bytes);
    error_text[bytes] = 0;
    if (controller)
        controller->error = error ? error : EIO;
}
const char *usb_storage_error(void)
{
    return error_text[0] ? error_text : "USB drive unavailable.";
}
const char *usb_storage_stage(void)
{
    return operation;
}
int usb_controller_error(void)
{
    return controller ? controller->error : last_error;
}
bool usb_storage_connected(void)
{
    return controller && controller->attached;
}
bool usb_controller_online(void)
{
    return controller && controller->online;
}
uint32_t usb_controller_generation(void)
{
    return controller ? controller->generation : next_generation;
}
uint32_t usb_storage_generation(void)
{
    return usb_controller_generation();
}
uint32_t usb_controller_sector_size(void)
{
    return controller ? controller->sector_size : 0;
}
uint64_t usb_controller_last_sector(void)
{
    return controller ? controller->last_sector : 0;
}
bool usb_storage_attachment_ready(void)
{
    return controller && controller->attached && !controller->init_failed;
}
bool usb_storage_driver_registered(void)
{
    return controller && controller->registered;
}
void usb_storage_stop_accepting(void)
{
    if (controller)
        controller->accepting = false;
}

bool usb_storage_supported(void)
{
    UsbRegisters registers;
    return usb_registers_current(&registers);
}

static bool reuse_storage_binding(UsbController *c, uint32_t native_free)
{
    uint32_t owner, device, interface;
    if (!native_usb_ui_storage_binding(&owner, &device, &interface)) return false;
    uint32_t driver = REG(owner);
    if (!ram((void *)(uintptr_t)driver, 36) || REG(driver + 4) != 2U || REG(driver + 12))
        return false;
    uint32_t methods = REG(driver);
    size_t code_offset = (sizeof(UsbCallbackStub) + 31U) & ~(size_t)31U;
    size_t code_bytes = (size_t)(usb_callback_end - usb_callback_start);
    if (methods < offsetof(UsbCallbackStub, methods)) return false;
    UsbCallbackStub *stub = (UsbCallbackStub *)(uintptr_t)(methods - offsetof(UsbCallbackStub, methods));
    if (!ram(stub, (unsigned)(code_offset + code_bytes)) || stub->host ||
        stub->active_callbacks || stub->retired || stub->allocation != stub ||
        (uint32_t)(uintptr_t)stub->native_free != native_free ||
        memcmp(stub->name, "ndvideo-usb", sizeof("ndvideo-usb"))) return false;
    const uint8_t *code = (const uint8_t *)stub + code_offset;
    size_t context_offset = (size_t)(usb_callback_context - usb_callback_start);
    if (stub->methods[0] != code + (usb_callback_match - usb_callback_start) ||
        stub->methods[1] != code + (usb_callback_attach - usb_callback_start) ||
        stub->methods[2] != code + (usb_callback_detach - usb_callback_start) ||
        stub->methods[6] != code + (usb_callback_cleanup - usb_callback_start) ||
        REG((uintptr_t)code + context_offset) != (uint32_t)(uintptr_t)stub ||
        memcmp(code, usb_callback_start, context_offset) ||
        memcmp(code + context_offset + 4, usb_callback_start + context_offset + 4,
               code_bytes - context_offset - 4)) return false;
    /* No native call or yield between authentication and publishing the new
     * callbacks. The OS still owns the device and its resident driver; reuse
     * that binding rather than adding another driver which cannot attach. */
    if (!ram((void *)(uintptr_t)interface, c->descriptor_offset + 4)) return false;
    struct usb_attach_arg args = {0};
    args.iface = (usbd_interface_handle)(uintptr_t)interface;
    if (!mass_storage(c, &args)) return false;
    c->stub = stub;
    c->device = (usbd_device_handle)(uintptr_t)device;
    c->interface = args.iface;
    c->native_owner = (device_t)(uintptr_t)owner;
    c->address = ((const uint8_t *)(uintptr_t)device)[8];
    c->speed = ((const uint8_t *)(uintptr_t)device)[11];
    c->registered = c->attached = c->reused_driver = true;
    c->generation = ++next_generation;
    stub->callbacks[0] = match;
    stub->callbacks[1] = attach;
    stub->callbacks[2] = detach;
    stub->callbacks[3] = cleanup;
    drain(NULL);
    stub->host = c;
    operation = "reuse storage binding";
    return true;
}

bool usb_storage_prepare_connection(void)
{
    UsbRegisters registers;
    operation = "register";
    error_text[0] = 0;
    last_error = 0;
    if (!usb_registers_current(&registers) || !nl_hassyscall(usb_register_driver) || !nl_hassyscall(usbd_open_pipe)) {
        usb_controller_set_error("USB storage is unavailable on this hardware.", ENOSYS);
        return false;
    }
    if (controller) {
        if (controller->registered) {
            controller->accepting = true;
            return true;
        }
        if (!usb_storage_shutdown())
            return false;
    }
    UsbController *c = calloc(1, sizeof(*c));
    if (!c) {
        usb_controller_set_error("Not enough memory for USB storage.", ENOMEM);
        return false;
    }
    controller = c;
    c->registers = registers;
    c->generation = ++next_generation;
    uint32_t a = export_abort(), b = export_close(), f = export_free(), m = export_alloc();
    if (!getter(export_ivars(), &c->ivars_offset) ||
        !getter(export_descriptor(), &c->descriptor_offset) || (a & 3) || (b & 3) || (f & 3) ||
        (m & 3) || !ram((void *)(uintptr_t)a, 4) || !ram((void *)(uintptr_t)b, 4) ||
        !ram((void *)(uintptr_t)f, 4) || !ram((void *)(uintptr_t)m, 4)) {
        usb_controller_set_error("The native USB interface could not be verified.", ENOSYS);
        goto fail;
    }
    c->abort_pipe = (usbd_status (*)(usbd_pipe_handle))(uintptr_t)a;
    c->close_pipe = (usbd_status (*)(usbd_pipe_handle))(uintptr_t)b;
    native_critical_enter();
    if (reuse_storage_binding(c, f)) return true;
    size_t code_offset = (sizeof(UsbCallbackStub) + 31U) & ~(size_t)31U;
    size_t code_bytes = (size_t)(usb_callback_end - usb_callback_start);
    /* SDK calloc/malloc add an alignment prefix. Resident cleanup calls OS
     * free after player
     * code is gone, so allocate this block through OS
     * malloc directly, using the same
     * allocator on both sides. */
    UsbCallbackStub *stub = ((void *(*)(size_t))(uintptr_t)m)(code_offset + code_bytes);
    if (!stub) {
        usb_controller_set_error("Not enough memory for USB callbacks.", ENOMEM);
        goto fail;
    }
    if (((uintptr_t)stub & 3U) || !ram(stub, (unsigned)(code_offset + code_bytes))) {
        ((void (*)(void *))(uintptr_t)f)(stub);
        usb_controller_set_error("USB callback memory could not be verified.", EIO);
        goto fail;
    }
    memset(stub, 0, code_offset + code_bytes);
    c->stub = stub;
    stub->callbacks[0] = match;
    stub->callbacks[1] = attach;
    stub->callbacks[2] = detach;
    stub->callbacks[3] = cleanup;
    stub->host = c;
    stub->native_free = (void (*)(void *))(uintptr_t)f;
    stub->allocation = stub;
    strcpy(stub->name, "ndvideo-usb");
    uint8_t *code = (uint8_t *)stub + code_offset;
    memcpy(code, usb_callback_start, code_bytes);
    *(uint32_t *)(code + (usb_callback_context - usb_callback_start)) = (uint32_t)(uintptr_t)stub;
    stub->methods[0] = code + (usb_callback_match - usb_callback_start);
    stub->methods[1] = code + (usb_callback_attach - usb_callback_start);
    stub->methods[2] = code + (usb_callback_detach - usb_callback_start);
    stub->methods[6] = code + (usb_callback_cleanup - usb_callback_start);
    clean(NULL, stub, code_offset + code_bytes);
    clear_cache();
    c->registered = c->accepting = true;
    int result = usb_register_driver(2, (int (**)(device_t))stub->methods, stub->name, 0, 64);
    if (result) {
        c->registered = c->accepting = false;
        usb_controller_set_error("Could not register the USB storage driver.", EIO);
        goto fail;
    }
    operation = "detect";
    return true;
fail:
    if (c->stub)
        c->stub->native_free(c->stub->allocation);
    free(c);
    controller = NULL;
    return false;
}
static int command_sync(const uint8_t *command, unsigned length, void *data, unsigned bytes)
{
    UsbController *c = controller;
    if (!c)
        return -1;
    unsigned mask = native_critical_enter();
    if (c->owner || !usb_scsi_begin(&c->scsi, command, length, data, bytes, true)) {
        native_critical_leave(mask);
        return -1;
    }
    UsbCommand request = {true, 0};
    c->owner = &request;
    int result;
    do {
        result = usb_controller_step(&request);
    } while (!result);
    native_critical_leave(mask);
    return result == 1 ? 0 : -1;
}
static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
bool usb_storage_finish_connection(void)
{
    UsbController *c = controller;
    operation = "open endpoints";
    if (!c || !c->attached) {
        usb_controller_set_error("No USB storage device was detected.", ENODEV);
        return false;
    }
    c->accepting = false;
    if (c->online)
        return true;
    if (c->transport.poisoned) {
        usb_controller_set_error("The USB controller needs to be reconnected.", EIO);
        return false;
    }
    close_pipes(c);
    free(c->arena);
    c->arena = NULL;
    c->endpoint_in = c->endpoint_out = 0;
    if (!usb_registers_storage_speed(&c->registers, c->speed) || !c->address || c->address > 127) {
        usb_controller_set_error("Unsupported USB storage connection speed.", ENOSYS);
        return false;
    }
    uint16_t packet_in = 0, packet_out = 0;
    uint8_t count = 0;
    if (usbd_endpoint_count(c->interface, &count) || count > 16)
        goto failed;
    for (unsigned i = 0; i < count; ++i) {
        usb_endpoint_descriptor_t *ep = usbd_interface2endpoint_descriptor(c->interface, i);
        if (!ep || !ram(ep, sizeof(*ep)) || ep->bLength < sizeof(*ep) ||
            ep->bDescriptorType != UDESC_ENDPOINT)
            goto failed;
        if (UE_GET_XFERTYPE(ep->bmAttributes) != UE_BULK)
            continue;
        if (!(ep->bEndpointAddress & 15U))
            goto failed;
        unsigned packet = UGETW(ep->wMaxPacketSize) & 0x7ff;
        if (c->speed == 3 ? packet != 512 : (packet != 8 && packet != 16 && packet != 32 && packet != 64))
            goto failed;
        if (UE_GET_DIR(ep->bEndpointAddress) == UE_DIR_IN) {
            if (c->endpoint_in)
                goto failed;
            c->endpoint_in = ep->bEndpointAddress;
            packet_in = packet;
        } else {
            if (c->endpoint_out)
                goto failed;
            c->endpoint_out = ep->bEndpointAddress;
            packet_out = packet;
        }
    }
    if (!c->endpoint_in || !c->endpoint_out)
        goto failed;
    if (usbd_open_pipe(c->interface, c->endpoint_in, USBD_EXCLUSIVE_USE, &c->input) ||
        usbd_open_pipe(c->interface, c->endpoint_out, USBD_EXCLUSIVE_USE, &c->output))
        goto failed;
    c->arena = malloc(USB_DMA_ALLOCATION_BYTES);
    if (!c->arena) {
        usb_controller_set_error("Not enough memory for USB transfers.", ENOMEM);
        goto failed;
    }
    uint8_t *bounce = (uint8_t *)(((uintptr_t)c->arena + 4095U) & ~(uintptr_t)4095U);
    if (!ram(bounce, USB_READ_BYTES + 96U))
        goto failed;
    UsbBus bus = {&c->registers, bus_read, bus_write, ticks, clean, invalidate, drain};
    usb_transport_init(&c->transport, bus, bounce, (UsbQueueHead *)(bounce + USB_READ_BYTES),
                       (UsbQtd *)(bounce + USB_READ_BYTES + 64U), (uint32_t)(uintptr_t)bounce,
                       (uint32_t)(uintptr_t)(bounce + USB_READ_BYTES), (uint32_t)(uintptr_t)(bounce + USB_READ_BYTES + 64U),
                       32768);
    c->transport.max_bytes = usb_controller_max_read_bytes();
    c->transport.high_speed = c->speed == 3;
    c->transport.packet_in = packet_in;
    c->transport.packet_out = packet_out;
    c->transport.address = c->address;
    c->transport.endpoint_in = c->endpoint_in;
    c->transport.endpoint_out = c->endpoint_out;
    usb_scsi_init(&c->scsi, &c->transport);
    operation = "identify";
    uint8_t data[36], inquiry[6] = {0x12, 0, 0, 0, 36, 0};
    if (command_sync(inquiry, 6, data, 36) || (data[0] & 31) != 0)
        goto failed;
    uint8_t ready[6] = {0};
    bool ready_ok = false;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        if (!command_sync(ready, 6, NULL, 0)) {
            ready_ok = true;
            break;
        }
        if (c->scsi.error != USB_SCSI_COMMAND_FAILED)
            break;
        uint8_t sense[6] = {3, 0, 0, 0, 18, 0};
        if (command_sync(sense, 6, data, 18))
            break;
    }
    if (!ready_ok)
        goto failed;
    uint8_t capacity[10] = {0x25};
    if (command_sync(capacity, 10, data, 8))
        goto failed;
    c->last_sector = be32(data);
    c->sector_size = be32(data + 4);
    if (c->last_sector == UINT32_MAX) {
        uint8_t extended[16]={0x9e,0x10};extended[13]=32;
        if(command_sync(extended,16,data,32))goto failed;
        c->last_sector=(uint64_t)be32(data)<<32|be32(data+4);
        c->sector_size=be32(data+8);
    }
    if (c->last_sector == UINT64_MAX || c->sector_size < 512 || c->sector_size > 4096 ||
        (c->sector_size & (c->sector_size - 1)))
        goto failed;
    c->online = true;
    c->init_failed = false;
    c->error = 0;
    error_text[0] = 0;
    operation = "ready";
    return true;
failed:
    if (!error_text[0])
        usb_controller_set_error("Could not initialize the USB drive.", EIO);
    c->online = false;
    c->init_failed = true;
    return false;
}
void usb_storage_poll_connection(void)
{
    if (controller && controller->online && !(REG(controller->registers.port) & 1U)) {
        controller->online = false;
        /* Do not reuse the old attachment until the native stack reports a
         * fresh attach after a physical disconnect. */
        controller->init_failed = true;
        controller->error = ENODEV;
        controller->generation = ++next_generation;
        strcpy(error_text, "The USB drive was disconnected.");
    }
}
bool usb_controller_parked(void)
{
    return controller && controller->parked;
}
bool usb_controller_begin_read(UsbCommand *request, uint64_t sector, unsigned count, void *data)
{
    UsbController *c = controller;
    usb_storage_poll_connection();
    if (!c || !request || request->pending || !c->online || c->owner || c->parked || !count ||
        count > usb_controller_max_read_bytes() / c->sector_size || sector > c->last_sector ||
        count - 1 > c->last_sector - sector)
        return false;
    uint8_t command[16];
    unsigned command_bytes=usb_scsi_read_command(command,sector,count);
    bool result = command_bytes && usb_scsi_begin(&c->scsi, command, command_bytes, data, count * c->sector_size, true);
    if (result) {
        ++c->commands;
        request->pending = true;
        request->result = 0;
        c->owner = request;
    } else {
        usb_controller_set_error("Could not start a USB read.", EIO);
        c->online = false;
        c->init_failed = true;
    }
    return result;
}
int usb_controller_step(UsbCommand *request)
{
    if (!request)
        return -1;
    if (!request->pending)
        return request->result;
    UsbController *c = controller;
    if (!c || c->owner != request)
        return -1;
    uint32_t start = ticks(NULL);
    int result = usb_scsi_step(&c->scsi);
    uint32_t elapsed = ticks(NULL) - start;
    c->cpu_ticks += elapsed;
    if (elapsed > c->max_step_ticks)
        c->max_step_ticks = elapsed;
    if (result < 0) {
        usb_controller_set_error(c->scsi.error == USB_TRANSPORT_DISCONNECTED
                                     ? "The USB drive was disconnected."
                                     : "USB read failed.",
                                 c->scsi.error == USB_TRANSPORT_DISCONNECTED ? ENODEV : EIO);
        c->online = false;
        c->init_failed = true;
    }
    if (result) {
        request->pending = false;
        request->result = result;
        c->owner = NULL;
    }
    return result;
}
bool usb_storage_park(void)
{
    UsbController *c = controller;
    if (!c)
        return true;
    c->parked = true;
    if (c->owner) {
        usb_controller_step(c->owner);
        return false;
    }
    /* A failed controller is disabled and its private DMA arena quarantined.
     * Do not trap unrelated local storage/exit work in an endless wait. */
    return usb_transport_quiescent(&c->transport) || c->transport.poisoned;
}
void usb_storage_unpark(void)
{
    if (controller)
        controller->parked = false;
}
bool usb_storage_quiescent(void)
{
    return !controller ||
           (!usb_scsi_busy(&controller->scsi) && usb_transport_quiescent(&controller->transport));
}
int usb_controller_read_sync(uint64_t sector, unsigned count, void *data)
{
    UsbCommand request = {0};
    UsbController *c = controller;
    if (!c || !c->online || c->owner)
        return -1;
    bool parked = c->parked;
    c->parked = false;
    int result = 0;
    while (count) {
        unsigned n = count;
        if (n > usb_controller_max_read_bytes() / c->sector_size)
            n = usb_controller_max_read_bytes() / c->sector_size;
        if (!usb_controller_begin_read(&request, sector, n, data)) {
            result = -1;
            break;
        }
        do {
            result = usb_controller_step(&request);
        } while (!result);
        if (result < 0)
            break;
        sector += n;
        count -= n;
        data = (uint8_t *)data + n * c->sector_size;
    }
    c->parked = parked;
    return result < 0 ? -1 : 0;
}
bool usb_storage_shutdown(void)
{
    usb_fs_set_progress(NULL, NULL);
    UsbController *c = controller;
    if (!c)
        return true;
    c->accepting = false;
    c->parked = true;
    while (c->owner)
        usb_controller_step(c->owner);
    if (c->stub && c->stub->active_callbacks) {
        unsigned cpu_mask = native_critical_enter();
        unsigned kernel_mask = TCT_Local_Control_Interrupts(0);
        uint32_t started = ticks(NULL);
        for (unsigned polls = 0; polls < 4000000U; ++polls) {
            native_critical_enter();
            if (!c->stub || !c->stub->active_callbacks)
                break;
            if ((uint32_t)(ticks(NULL) - started) >= 32768U)
                break;
            native_critical_leave(0);
        }
        native_critical_enter();
        TCT_Local_Control_Interrupts(kernel_mask);
        native_critical_leave(cpu_mask);
        if (c->stub && c->stub->active_callbacks) {
            usb_controller_set_error("A USB callback is still finishing.", EBUSY);
            return false;
        }
    }
    usb_volume_unmount();
    if (usb_transport_quiescent(&c->transport)) {
        close_pipes(c);
        free(c->arena);
    }
    /* If DMA could not be stopped, its separate allocation is quarantined.
     * It never refers into the player image or decoder buffers. */
    if (c->stub) {
        unsigned mask = native_critical_enter();
        c->stub->host = NULL;
        drain(NULL);
        bool registered = c->registered;
        native_critical_leave(mask);
        if (!registered)
            c->stub->native_free(c->stub->allocation);
    }
    last_error = c->error;
    controller = NULL;
    ++next_generation;
    free(c);
    operation = "idle";
    return true;
}
void usb_controller_debug(FILE *file)
{
    UsbController *c = controller;
    if (!file)
        return;
    if (c) fprintf(file, "usb_read_path max_command_bytes=%u link_speed=%u packet_in=%u packet_out=%u bytes=%lu argument_aliases=%lu\n", usb_controller_max_read_bytes(), c->speed, c->transport.packet_in, c->transport.packet_out, (unsigned long)c->read_bytes, (unsigned long)c->argument_aliases);
    UsbRegisters registers;
    if (!usb_registers_current(&registers)) return;
    fprintf(file,
            "usb_controller cap=%08lx command=%08lx status=%08lx port=%08lx otg=%08lx "
            "interrupts=%08lx otg_mask=%08lx\n",
            (unsigned long)REG(registers.capability), (unsigned long)REG(registers.command),
            (unsigned long)REG(registers.command + 4), (unsigned long)REG(registers.port),
            (unsigned long)REG(registers.otg), (unsigned long)REG(registers.command + 8),
            (unsigned long)REG(registers.otg_mask));
    if (!c) return;
    fprintf(file,
            "usb_reader online=%u generation=%lu match=%lu attach=%lu detach=%lu cleanup=%lu "
            "commands=%lu max_step_ticks=%lu cpu_ticks=%lu error=%d stage=%s token=%08lx "
            "transport_error=%u poisoned=%u registered=%u accepting=%u attached=%u\n",
            c->online, (unsigned long)c->generation, (unsigned long)c->matches,
            (unsigned long)c->attaches, (unsigned long)c->detaches, (unsigned long)c->cleanups,
            (unsigned long)c->commands, (unsigned long)c->max_step_ticks,
            (unsigned long)c->cpu_ticks, c->error, operation,
            (unsigned long)c->transport.last_token, c->transport.error, c->transport.poisoned,
            c->registered, c->accepting, c->attached);
    fprintf(file, "usb_deadline phase=%u elapsed_ticks=%lu stalled_polls=%lu total_polls=%lu scsi_phase=%u expected=%u transferred=%u reused_driver=%u\n",
            c->transport.timeout_phase, (unsigned long)c->transport.timeout_elapsed,
            (unsigned long)c->transport.timeout_polls, (unsigned long)c->transport.total_polls,
            c->scsi.phase, c->scsi.expected, c->scsi.transferred, c->reused_driver);
    unsigned count = c->matches < 4 ? c->matches : 4;
    for (unsigned i = c->matches - count; i < c->matches; ++i) {
        const uint32_t *t = c->match_trace[i % 4];
        fprintf(file, "usb_match sequence=%lu owner=%08lx args=%08lx iface=%08lx descriptor=%08lx reason=%lu class=%06lx\n",
                (unsigned long)i + 1, (unsigned long)t[0], (unsigned long)t[1],
                (unsigned long)t[2], (unsigned long)t[3], (unsigned long)t[4], (unsigned long)t[5]);
    }
}
void usb_storage_debug(FILE *file)
{
    usb_controller_debug(file);
    usb_fs_debug(file);
}
size_t usb_storage_memory_bytes(void)
{
    return usb_fs_memory_bytes() +
           (controller ? sizeof(*controller) + (controller->arena ? USB_DMA_ALLOCATION_BYTES : 0U) +
                             (controller->stub ? ((sizeof(UsbCallbackStub) + 31U) & ~31U) +
                                                     (size_t)(usb_callback_end - usb_callback_start)
                                               : 0U)
                       : 0U);
}
