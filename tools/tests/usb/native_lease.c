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
static bool mapping_valid,tree_valid,bus_borrowed;
static uint32_t borrow_usb_lock,borrow_tree_lock;
static unsigned borrow_lock_stage;
static uint32_t bus[451],handle[9],registers[64];
static uint32_t *test_word(uint32_t address)
{
    if(address>=0x1000 && address<0x1000+sizeof(bus))return bus+(address-0x1000)/4;
    if(address>=0x2000 && address<0x2000+sizeof(handle))return handle+(address-0x2000)/4;
    assert(address>=0x3000 && address<0x3000+sizeof(registers));
    return registers+(address-0x3000)/4;
}
#define WORD(address) (*test_word(address))
static bool portable_reader_platform_refresh(unsigned *view)
{assert(view==&mapping);return mapping_valid;}
static bool resume_tree(UsbHostTree *tree,uint32_t *resume)
{*tree=(UsbHostTree){0x1000,0x2000};*resume=1;return tree_valid;}
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
    mapping_valid=tree_valid=true;bus_borrowed=false;
    borrow_usb_lock=borrow_tree_lock=borrow_lock_stage=0;
    bus[604/4]=0x11000000;bus[1772/4]=1;
    registers[0]=0x01000010;registers[4]=0x80031;registers[12]=5;
}
int main(void)
{
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
