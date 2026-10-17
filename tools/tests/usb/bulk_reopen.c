/* Real endpoint setup and BOT recovery against a retained device carrying
 * DATA1 and a pending CSW. No USB controller or filesystem is emulated here. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "usb_scsi.h"
typedef void *device_t,*usbd_device_handle,*usbd_interface_handle,*usbd_pipe_handle;
typedef int usbd_status;
typedef unsigned UsbRegisters;
typedef struct {bool pending;int result;} UsbCommand;
typedef struct {uint8_t bLength,bDescriptorType,bInterfaceNumber,bAlternateSetting,bNumEndpoints,
    bInterfaceClass,bInterfaceSubClass,bInterfaceProtocol,iInterface;} usb_interface_descriptor_t;
typedef struct {uint8_t bLength,bDescriptorType,bEndpointAddress,bmAttributes,wMaxPacketSize[2],bInterval;} usb_endpoint_descriptor_t;
typedef struct {uint8_t bmRequestType,bRequest,wValue[2],wIndex[2],wLength[2];} usb_device_request_t;
#define USETW(w,v) ((w)[0]=(uint8_t)(v),(w)[1]=(uint8_t)((v)>>8))
#define UGETW(w) ((unsigned)(w)[0]|(unsigned)(w)[1]<<8)
#define UDESC_INTERFACE 4
#define UDESC_ENDPOINT 5
#define UICLASS_MASS 8
#define UISUBCLASS_SCSI 6
#define UIPROTO_MASS_BBB 80
#define UE_BULK 2
#define UE_DIR_IN 0x80
#define UE_GET_XFERTYPE(v) ((v)&3)
#define UE_GET_DIR(v) ((v)&0x80)
#define USBD_EXCLUSIVE_USE 1
#define UT_WRITE_CLASS_INTERFACE 0x21
#define USB_DMA_ALLOCATION_BYTES (USB_READ_BYTES+96U+4095U)
/* PRODUCTION_TYPES */
static UsbController *controller;
static const char *operation;
static char error_text[128];
static bool available,device_pending_csw,native_lease;
static unsigned step,fail_at,detach_at,mask,device_in_toggle,device_out_toggle,pipe_closes;
static unsigned registers;
static uint8_t device;
static usb_interface_descriptor_t descriptor={9,4,7,0,2,8,6,80,0};
static void *interface_slot;
static usb_endpoint_descriptor_t endpoints[2]={{7,5,0x81,2,{0,2},0},{7,5,0x02,2,{0,2},0}};
static void *input=(void *)(uintptr_t)1,*output=(void *)(uintptr_t)2;
#define nl_hassyscall(name) available
static bool ram(const void *p,unsigned bytes){(void)bytes;return p!=NULL;}
static bool usb_registers_storage_speed(unsigned *r,unsigned speed){(void)r;return speed==3;}
static unsigned usb_controller_max_read_bytes(void){return USB_READ_BYTES;}
static unsigned native_critical_enter(void){unsigned old=mask;mask=0xc0;return old;}
static void native_critical_leave(unsigned old){mask=old;}
static bool native_usb_ui_bus_is_leased(void){return native_lease;}
static void usb_controller_set_error(const char *message,int error)
{snprintf(error_text,sizeof(error_text),"%s",message);controller->error=error;}
static void close_pipes(UsbController *c){if(c->input||c->output)++pipe_closes;c->input=c->output=NULL;}
static int usbd_endpoint_count(void *interface,uint8_t *count)
{assert(interface==&interface_slot);*count=2;return 0;}
static usb_endpoint_descriptor_t *usbd_interface2endpoint_descriptor(void *interface,unsigned n)
{assert(interface==&interface_slot && n<2);return endpoints+n;}
static int usbd_open_pipe(void *interface,unsigned endpoint,unsigned flags,void **pipe)
{assert(interface==&interface_slot && flags==1);*pipe=endpoint==0x81?input:output;return 0;}
static void lose_device(void)
{if(step==detach_at){controller->attached=false;controller->device=NULL;controller->input=controller->output=NULL;}}
static int usbd_do_request(void *dev,usb_device_request_t *request,void *data)
{
    assert(!mask && dev==&device && !data && !controller->arena && !controller->transport.owns_schedule && !step);
    assert(request->bmRequestType==0x21 && request->bRequest==0xff && !UGETW(request->wValue) &&
        UGETW(request->wIndex)==7 && !UGETW(request->wLength));
    ++step;lose_device();if(step==fail_at)return 13;
    device_pending_csw=false; /* Class reset deliberately preserves DATA toggles. */
    return 0;
}
static int usbd_clear_endpoint_stall(void *pipe)
{
    assert(!mask && !controller->arena && !controller->transport.owns_schedule && step>=1 && step<=2);
    assert(pipe==(step==1?input:output));++step;lose_device();if(step==fail_at)return 13;
    if(pipe==input)device_in_toggle=0;else device_out_toggle=0;
    return 0;
}
static uint32_t bus_read(void *c,unsigned o){(void)c;(void)o;return 0;}
static void bus_write(void *c,unsigned o,uint32_t v){(void)c;(void)o;(void)v;}
static uint32_t ticks(void *c){(void)c;return 0;}
static void clean(void *c,void *p,size_t n){(void)c;(void)p;(void)n;}
static void invalidate(void *c,void *p,size_t n){(void)c;(void)p;(void)n;}
static void drain(void *c){(void)c;}
/* PRODUCTION_BULK_REOPEN */
static void reset(UsbController *c,bool reused)
{
    memset(c,0,sizeof(*c));controller=c;c->registers=registers;c->attached=true;
    c->device=&device;c->interface=&interface_slot;c->speed=3;c->address=5;c->reused_driver=reused;
    c->attachment_count=1;c->attachments[0].reset_required=reused;
    interface_slot=&descriptor;descriptor.bInterfaceClass=8;
    step=fail_at=detach_at=pipe_closes=0;mask=0xc0;available=true;native_lease=false;error_text[0]=0;
    device_in_toggle=device_out_toggle=reused?1:0;device_pending_csw=reused;
}
int main(void)
{
    UsbController c;
    reset(&c,false);assert(usb_storage_prepare_endpoints());assert(!step && !c.bulk_resets);free(c.arena);
    reset(&c,true);assert(usb_storage_prepare_endpoints());
    assert(step==3 && c.bulk_resets==1 && !device_pending_csw && !device_in_toggle && !device_out_toggle &&
        !c.transport.toggle_in && !c.transport.toggle_out && !c.bulk_reset_result &&
        !c.bulk_clear_in_result && !c.bulk_clear_out_result && mask==0xc0);
    c.online=true;assert(usb_storage_prepare_endpoints() && step==3);free(c.arena);
    reset(&c,false);assert(usb_storage_prepare_endpoints() && !step);
    c.scsi.tag=9;device_pending_csw=true;device_in_toggle=device_out_toggle=1;
    assert(usb_storage_prepare_endpoints() && step==3 && !device_pending_csw);free(c.arena);
    reset(&c,false);assert(usb_storage_prepare_endpoints() && !step);
    c.scsi.tag=9;c.attachment_count=2;c.selected_attachment=1;
    assert(usb_storage_prepare_endpoints() && !step);free(c.arena); /* Used sibling does not reset this fresh device. */
    for(unsigned fail=1;fail<=3;++fail) {
        reset(&c,true);fail_at=fail;
        assert(!usb_storage_prepare_endpoints() && step==fail && c.init_failed && !c.online && !c.arena && c.error==EIO && mask==0xc0);
    }
    for(unsigned lost=1;lost<=3;++lost) {
        reset(&c,true);detach_at=lost;
        assert(!usb_storage_prepare_endpoints() && step==lost && !c.arena && mask==0xc0);
    }
    reset(&c,true);available=false;assert(!usb_storage_prepare_endpoints() && !step && !c.arena && c.error==ENOSYS);
    reset(&c,true);descriptor.bInterfaceClass=0xff;assert(!usb_storage_prepare_endpoints() && !step && !c.arena);
    reset(&c,true);c.transport.owns_schedule=true;assert(!usb_storage_prepare_endpoints() && !step && !pipe_closes && c.error==EBUSY);
    reset(&c,true);native_lease=true;assert(!usb_storage_prepare_endpoints() && !step && !pipe_closes && c.error==EBUSY);
    reset(&c,true);c.scsi.phase=USB_SCSI_CSW;assert(!usb_storage_prepare_endpoints() && !step && !pipe_closes && c.error==EBUSY);
    puts("PASS reused/cancelled BOT sessions reset protocol then clear IN/OUT toggles before DATA0; fresh/online paths stay fast; failures, detach, foreign interface and active DMA refuse takeover");
    return 0;
}
