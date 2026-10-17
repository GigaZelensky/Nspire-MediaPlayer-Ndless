#define _GNU_SOURCE
#include "usb_scsi.h"
#include "usb_controller.h"
#include "usb_registers.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#undef interface
#else
#include <sys/mman.h>
#endif
typedef void *device_t, *usbd_device_handle, *usbd_interface_handle, *usbd_pipe_handle;
typedef int usbd_status;
struct usb_attach_arg { usbd_interface_handle iface; };
/* PRODUCTION_TYPES */
static const uint8_t template_code[32] = {1,2,3,4,5,6,7,8};
#define usb_callback_start template_code
#define usb_callback_end (template_code + 32)
#define usb_callback_context (template_code + 28)
#define usb_callback_match template_code
#define usb_callback_attach (template_code + 4)
#define usb_callback_detach (template_code + 8)
#define usb_callback_cleanup (template_code + 12)
#define REG(at) (*(uint32_t *)(uintptr_t)(at))
static uint8_t *arena;
static uint32_t next_generation;
static const char *operation;
static unsigned reuse_stage;
static char error_text[128];
static bool candidate_valid, interface_valid;
static int match(device_t d, UsbController *c) { (void)d; (void)c; return 0; }
static int attach(device_t d, UsbController *c) { (void)d; (void)c; return 0; }
static int detach(device_t d, UsbController *c) { (void)d; (void)c; return 0; }
static int cleanup(device_t d, UsbController *c) { (void)d; (void)c; return 0; }
static void drain(void *unused) { (void)unused; }
static bool ram(const void *p, unsigned bytes)
{
    return (uintptr_t)p >= (uintptr_t)arena && (uintptr_t)p <= (uintptr_t)arena + 4096U - bytes;
}
static bool mass_storage(UsbController *c, struct usb_attach_arg *a)
{ (void)c; return interface_valid && a->iface == (void *)(arena + 512); }
static bool native_usb_ui_storage_binding(uint32_t *owner, uint32_t *device, uint32_t *interface)
{
    *owner = (uint32_t)(uintptr_t)arena;
    *device = (uint32_t)(uintptr_t)(arena + 256);
    *interface = (uint32_t)(uintptr_t)(arena + 512);
    return candidate_valid;
}
static bool native_usb_ui_storage_binding_after(unsigned after,uint32_t *owner,uint32_t *device,uint32_t *interface)
{(void)after;(void)owner;(void)device;(void)interface;return false;}
/* PRODUCTION_ATTACHMENTS */
/* PRODUCTION_REUSE */
static UsbController *controller;
static void close_pipes(UsbController *c) {(void)c;}
bool usb_transport_quiescent(const UsbTransport *t) {return !t->owns_schedule;}
/* PRODUCTION_FINISH */
static uint32_t audio_tree[8];
static void native_usb_ui_bus_snapshot(uint32_t out[8]) {memcpy(out,audio_tree,sizeof(audio_tree));}
/* PRODUCTION_AUDIO_CLASSIFICATION */
static UsbCallbackStub *setup(void)
{
    memset(arena, 0, 4096);
    candidate_valid = interface_valid = true;
    uint32_t driver = (uint32_t)(uintptr_t)(arena + 128);
    UsbCallbackStub *stub = (UsbCallbackStub *)(void *)(arena + 1024);
    REG(arena) = driver;
    REG(driver) = (uint32_t)(uintptr_t)stub->methods;
    REG(driver + 4) = 2;
    stub->allocation = stub;
    stub->native_free = (void (*)(void *))(uintptr_t)0x11111111U;
    strcpy(stub->name, "ndvideo-usb");
    uint8_t *code = (uint8_t *)stub + ((sizeof(*stub) + 31U) & ~(size_t)31U);
    memcpy(code, template_code, sizeof(template_code));
    REG(code + 28) = (uint32_t)(uintptr_t)stub;
    stub->methods[0] = code;
    stub->methods[1] = code + 4;
    stub->methods[2] = code + 8;
    stub->methods[6] = code + 12;
    arena[256 + 8] = 2;
    arena[256 + 11] = 3;
    return stub;
}
int main(void)
{
#ifdef _WIN32
    arena = VirtualAlloc((void *)(uintptr_t)0x11000000U, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    arena = mmap((void *)(uintptr_t)0x11000000U, 4096, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (arena == MAP_FAILED) arena = NULL;
#endif
    assert(arena && (uintptr_t)arena <= UINT32_MAX - 4096);
    UsbCallbackStub *stub = setup();
    UsbController c = {0};
    assert(usb_registers_for_asic(0x202,&c.registers));
    assert(reuse_storage_binding(&c, 0x11111111));
    assert(c.stub == stub && stub->host == &c && stub->callbacks[0] == match &&
           stub->callbacks[1] == attach && stub->callbacks[2] == detach && stub->callbacks[3] == cleanup);
    assert(c.registered && c.attached && c.reused_driver && c.address == 2 && c.speed == 3);
    assert(c.attachment_count==1 && c.attachments[0].present);
    assert(remember_attachment(&c,(void *)arena,(void *)(arena+256),(void *)(arena+512)));
    assert(c.attachment_count==1);
    arena[768+8]=3;arena[768+11]=3;
    assert(remember_attachment(&c,(void *)(arena+128),(void *)(arena+768),(void *)(arena+800)));
    assert(c.attachment_count==2 && c.address==2);
    select_attachment(&c,1);assert(c.address==3 && c.device==(void *)(arena+768));
    controller=&c;
    assert(usb_storage_next_candidate() && c.address==2);
    assert(usb_storage_next_candidate() && c.address==3);
    assert(!usb_storage_next_candidate());
    assert(c.attachments[0].tried && c.attachments[1].tried);
    assert(!usb_storage_attachment_ready());
    arena[864+8]=4;arena[864+11]=3;
    assert(remember_attachment(&c,(void *)(arena+192),(void *)(arena+864),(void *)(arena+896)));
    /* Native pipe close can yield to a late attach before an older probe
     * writes its failure flag. Pending attachments must remain discoverable. */
    c.init_failed=true;
    assert(usb_storage_attachment_ready());
    assert(usb_storage_next_candidate() && c.address==4);
    assert(!usb_storage_attachment_ready());
    c.attachments[0].tried=c.attachments[1].tried=false;c.transport.poisoned=true;
    assert(!usb_storage_next_candidate() && !c.attachments[0].tried);
    c.transport.poisoned=false;c.transport.owns_schedule=true;
    assert(!usb_storage_next_candidate() && !c.attachments[0].tried);
    controller=NULL;
    for (unsigned i = 0; i < 10; ++i) {
        c = (UsbController){0}; stub = setup();
        uint8_t *code = (uint8_t *)stub + ((sizeof(*stub) + 31U) & ~(size_t)31U);
        switch (i) {
        case 0: stub->host = &c; break;
        case 1: stub->active_callbacks = 1; break;
        case 2: stub->retired = 1; break;
        case 3: stub->name[0] = 'x'; break;
        case 4: stub->methods[1] = code + 8; break;
        case 5: code[0] ^= 1; break;
        case 6: REG(code + 28) ^= 4; break;
        case 7: REG(arena + 132) = 1; break;
        case 8: candidate_valid = false; break;
        case 9: interface_valid = false; break;
        }
        assert(!reuse_storage_binding(&c, 0x11111111));
        assert(!c.stub && !c.registered && !c.reused_driver && !stub->callbacks[0]);
    }
    c = (UsbController){0}; setup();
    assert(!reuse_storage_binding(&c, 0x22222222) && !c.stub);
    assert(!usb_controller_audio_only());
    controller=&c;c=(UsbController){.interface_classes=1U<<1};
    audio_tree[7]=1;assert(!usb_controller_audio_only());
    audio_tree[0]=1;assert(usb_controller_audio_only());
    for(unsigned count=0;count<6;++count) {
        audio_tree[7]=count;assert(usb_controller_audio_only()==(count==1));
    }
    audio_tree[7]=1;c.interface_classes|=1U<<8;assert(!usb_controller_audio_only());
    c.interface_classes=1U<<3;assert(!usb_controller_audio_only());
    c.interface_classes=1U<<1;c.attached=true;assert(!usb_controller_audio_only());
    puts("PASS audio-only classification: valid single headset, hubs, unknown tree, storage and non-audio interfaces");
    puts("PASS resident USB binding reuse; foreign, active, retired, changed-code, context, allocator and interface rejection");
    return 0;
}
