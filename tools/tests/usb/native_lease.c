/* Actual retain predicate with an authenticated-tree boundary and fake MMIO. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint32_t bus,handle;} UsbHostTree;
typedef struct {uint32_t capability,command,port,otg,role_mask;} UsbRegisters;
static struct {bool classic_cx;} api;
static unsigned mapping;
static bool mapping_valid,tree_valid,bus_borrowed,resolved=true;
static uint32_t borrow_usb_lock,borrow_tree_lock;
static unsigned borrow_lock_stage;
static uint32_t bus[451],handle[9],registers[64],head[20];
static uint32_t *test_word(uint32_t address)
{
    if(address>=0x1000 && address<0x1000+sizeof(bus))return bus+(address-0x1000)/4;
    if(address>=0x2000 && address<0x2000+sizeof(handle))return handle+(address-0x2000)/4;
    if(address>=0x2100 && address<0x2100+sizeof(head))return head+(address-0x2100)/4;
    assert(address>=0x3000 && address<0x3000+sizeof(registers));
    return registers+(address-0x3000)/4;
}
#define WORD(address) (*test_word(address))
static bool portable_reader_platform_refresh(unsigned *view)
{assert(view==&mapping);return mapping_valid;}
static bool resume_tree(UsbHostTree *tree,uint32_t *resume)
{*tree=(UsbHostTree){0x1000,0x2000};*resume=1;return tree_valid;}
static bool host_tree(UsbHostTree *tree)
{*tree=(UsbHostTree){0x1000,0x2000};return tree_valid;}
static bool memory(uint32_t address,unsigned bytes)
{return address>=0x2100 && bytes<=sizeof(head) && address-0x2100<=sizeof(head)-bytes;}
static unsigned native_critical_enter(void) {return 0xc0;}
static void native_critical_leave(unsigned mask) {assert(mask==0xc0);}
static bool usb_registers_for_asic(uint32_t asic,UsbRegisters *r)
{
    assert(asic==0x202);
    *r=(UsbRegisters){0x3000,0x3010,0x3030,0x3080,1U<<21};return true;
}
static bool usb_registers_valid(const UsbRegisters *r)
{return WORD(r->capability)==0x01000010;}
/* PRODUCTION_NATIVE_LEASE */
static void reset(void)
{
    memset(bus,0,sizeof(bus));memset(handle,0,sizeof(handle));
    memset(registers,0,sizeof(registers));
    memset(head,0,sizeof(head));
    mapping_valid=tree_valid=true;bus_borrowed=false;
    borrow_usb_lock=borrow_tree_lock=borrow_lock_stage=0;
    bus[604/4]=0x11000000;bus[1772/4]=1;
    registers[0]=0x01000010;registers[4]=0x80031;registers[12]=5;
}
int main(void)
{
    /* The diagnostic samples native metadata and CPU-view queue fields only.
     * It must not write a register, queue, semaphore, or cache-control state. */
    reset();bus[1740/4]=0x2100;head[76/4]=0x11000000;head[0]=0x11000002;
    head[68/4]=0x2200;bus[1736/4]=0x3f;bus[1764/4]=0x2300;
    registers[7]=1234;registers[10]=0x11000000;
    uint32_t saved_bus[451],saved_head[20],saved_registers[64],details[16];
    memcpy(saved_bus,bus,sizeof(bus));memcpy(saved_head,head,sizeof(head));memcpy(saved_registers,registers,sizeof(registers));
    native_usb_ui_bus_details(details);
    assert(details[0] && details[2]==0x3f && details[5]==0x2300 && details[6]==0x2100 &&
           details[7]==0x11000000 && details[8]==0x11000002 && details[11]==0x2200 &&
           details[13]==1234 && details[14]==0x11000000);
    assert(!memcmp(saved_bus,bus,sizeof(bus)) && !memcmp(saved_head,head,sizeof(head)) &&
           !memcmp(saved_registers,registers,sizeof(registers)));
    tree_valid=false;native_usb_ui_bus_details(details);for(unsigned i=0;i<16;++i)assert(!details[i]);
    reset();bus[1740/4]=0xfffffff0;native_usb_ui_bus_details(details);assert(details[0] && !details[7]);
    puts("PASS native queue diagnostic is read-only, validates pointers and rejects an unknown tree");
    reset();registers[0x40/4]=0x40;registers[0x84/4]=0x400;registers[0x88/4]=0x1234;
    registers[0xc0/4]=2;registers[0xc4/4]=1;
    memcpy(saved_registers,registers,sizeof(registers));
    uint32_t phy[10];native_usb_ui_bus_phy(phy);
    assert(phy[0] && phy[1]==0x80031 && phy[3]==5 && phy[5]==0x40 && phy[6]==0x400 &&
        phy[7]==0x1234 && phy[8]==2 && phy[9]==1 && !memcmp(saved_registers,registers,sizeof(registers)));
    api.classic_cx=true;native_usb_ui_bus_phy(phy);for(unsigned i=0;i<10;++i)assert(!phy[i]);
    api.classic_cx=false;registers[0]=0;native_usb_ui_bus_phy(phy);for(unsigned i=0;i<10;++i)assert(!phy[i]);
    puts("PASS role/PHY capture is read-only and rejects classic-CX or an unreadable controller");
    for(unsigned mode=0;mode<7;++mode) {
        reset();
        switch(mode) {
        case 0:mapping_valid=false;break;
        case 1:tree_valid=false;break;
        case 2:bus[604/4]=0;break;
        case 3:bus[1772/4]=0;break;
        case 4:bus[1760/4]=1;break;
        case 5:handle[32/4]=1;break;
        case 6:bus[1760/4]=handle[32/4]=1;break;
        }
        assert(!retain_bus_lease(11,22));
        assert(!bus_borrowed && !borrow_usb_lock && !borrow_tree_lock && !borrow_lock_stage);
    }
    reset();assert(retain_bus_lease(11,22));
    assert(bus_borrowed && borrow_usb_lock==11 && borrow_tree_lock==22 && borrow_lock_stage==3);
    assert(!retain_bus_lease(33,44) && borrow_usb_lock==11 && borrow_tree_lock==22);
    puts("PASS native resume retains a lease only for an authenticated running, registered and resumed host");
    return 0;
}
