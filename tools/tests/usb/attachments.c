/* Production attach/detach bookkeeping, with native handles and pipes stubbed. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
typedef void *device_t;
typedef void *usbd_device_handle;
typedef void *usbd_interface_handle;
typedef struct {device_t owner;void *device,*interface;uint8_t address,speed,sense_key,asc,ascq;bool present,tried,reset_required;} UsbAttachment;
typedef struct {
    UsbAttachment attachments[16];unsigned attachment_count,selected_attachment;
    void *native_owner,*device,*interface;unsigned address,speed,generation;
    bool attached,online,init_failed;int error;unsigned sector_size,endpoint_in,endpoint_out;
    uint64_t last_sector;unsigned registers,transport,detaches;
} UsbController;
static unsigned next_generation;
static char error_text[128];
static bool ram(const void *p,unsigned bytes){return p && bytes==12;}
static bool usb_registers_storage_speed(unsigned *r,unsigned speed){(void)r;return speed==2 || speed==3;}
static unsigned native_critical_enter(void){return 0xc0;}
static void native_critical_leave(unsigned mask){assert(mask==0xc0);}
static bool usb_transport_quiescent(unsigned *t){(void)t;return true;}
static void close_pipes(UsbController *c){(void)c;}
/* PRODUCTION_ATTACHMENTS */
int main(void)
{
    UsbController c={0};uint8_t device[17][12]={{0}};
    for(unsigned cycle=0;cycle<64;++cycle){
        /* Native allocators can recycle the owner handle, or allocate a new
         * one. Neither may retain the old device/iface or a failed-read flag. */
        unsigned index=cycle%17;device[index][8]=(uint8_t)(index+1);device[index][11]=3;
        void *owner=(void *)(uintptr_t)(0x1000+cycle%2*4);
        void *iface=(void *)(uintptr_t)(0x2000+cycle*4);
        assert(remember_attachment(&c,owner,device[index],iface,false));
        assert(c.attached && c.attachment_count==1 && c.device==device[index] && c.interface==iface);
        assert(c.address==index+1 && !c.attachments[0].tried && !c.init_failed);
        c.attachments[0].tried=true;c.init_failed=true;
        assert(detach(owner,&c)==0 && !c.attached && !c.attachments[0].present);
    }
    memset(&c,0,sizeof(c));
    for(unsigned i=0;i<17;++i){
        device[i][8]=(uint8_t)(i+1);device[i][11]=2;
        bool added=remember_attachment(&c,(void *)(uintptr_t)(i+1),device[i],device[i],false);
        assert(added==(i<16));
    }
    assert(c.attachment_count==16);
    assert(detach((void *)8,&c)==0);
    assert(remember_attachment(&c,(void *)17,device[16],device[16],false));
    assert(c.attachment_count==16 && c.attachments[7].present && c.attachments[7].address==17);
    puts("PASS production USB attachments: 64 detach/reinsert cycles, recycled owners, slot reuse and live capacity");
    return 0;
}
