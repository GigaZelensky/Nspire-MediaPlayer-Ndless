/* Native state fallback reads the leased, authenticated USBdi objects only. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "usb_route.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
typedef struct {uint32_t bus;} UsbHostTree;
static uint8_t *arena;
static bool bus_borrowed=true, route_valid=true, tree_valid=true;
static uint32_t base;
#define WORD(at) (*(uint32_t *)(uintptr_t)(at))
static bool memory(uint32_t at,unsigned bytes)
{return bytes<=4096 && at>=base && at<=base+4096-bytes;}
static bool native_usb_ui_bus_route(void *c,uint8_t address,UsbDeviceRoute *out)
{(void)c;(void)out;return bus_borrowed && route_valid && address==4;}
static unsigned native_critical_enter(void){return 0xc0;}
static void native_critical_leave(unsigned mask){assert(mask==0xc0);}
static bool host_tree(UsbHostTree *out){out->bus=base;return tree_valid;}
/* PRODUCTION_STATE */
int main(void)
{
#ifdef _WIN32
    arena=VirtualAlloc((void *)(uintptr_t)0x11000000,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    arena=mmap((void *)(uintptr_t)0x11000000,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(arena==MAP_FAILED)arena=NULL;
#endif
    assert(arena && (uintptr_t)arena<=UINT32_MAX-4096);base=(uint32_t)(uintptr_t)arena;
    uint32_t device=base+1024,config=base+1280,array=base+1536;
    WORD(base+52+4*4)=device;WORD(device)=base;WORD(device+84)=config;WORD(device+60)=array;
    const uint8_t descriptors[]={9,2,36,0,2,1,0,0x80,50,
        9,4,0,0,0,1,1,0,0,9,4,1,0,0,1,2,0,0,9,4,1,1,1,1,2,0,0};
    memcpy((void *)(uintptr_t)config,descriptors,sizeof(descriptors));arena[1024+9]=1;
    WORD(array)=WORD(array+32)=device;WORD(array+8)=config+9;WORD(array+32+8)=config+18;
    UsbDeviceState state;
    assert(native_usb_ui_bus_device_state(NULL,4,-1,&state) && state.configuration==1 && !state.alternate_known);
    assert(native_usb_ui_bus_device_state(NULL,4,1,&state) && state.configuration==1 && state.alternate_known && !state.alternate);
    WORD(array+32+8)=config+27;
    assert(native_usb_ui_bus_device_state(NULL,4,1,&state) && state.alternate_known && state.alternate==1);
    WORD(array+32+8)=config+18;
    WORD(array+32+28)=1;assert(native_usb_ui_bus_device_state(NULL,4,1,&state) && !state.alternate_known);WORD(array+32+28)=0;
    WORD(array+32+4)=1;assert(native_usb_ui_bus_device_state(NULL,4,1,&state) && !state.alternate_known);WORD(array+32+4)=0;
    WORD(array+32+8)=config+19;assert(native_usb_ui_bus_device_state(NULL,4,1,&state) && !state.alternate_known);WORD(array+32+8)=config+18;
    WORD(array+32)=0;assert(native_usb_ui_bus_device_state(NULL,4,1,&state) && !state.alternate_known);WORD(array+32)=device;
    arena[1280+5]=2;assert(!native_usb_ui_bus_device_state(NULL,4,-1,&state));arena[1280+5]=1;
    arena[1280+18]=0;assert(!native_usb_ui_bus_device_state(NULL,4,-1,&state));arena[1280+18]=9;
    WORD(device+84)=base+4090;assert(!native_usb_ui_bus_device_state(NULL,4,-1,&state));WORD(device+84)=config;
    route_valid=false;assert(!native_usb_ui_bus_device_state(NULL,4,-1,&state));route_valid=true;
    bus_borrowed=false;assert(!native_usb_ui_bus_device_state(NULL,4,-1,&state));bus_borrowed=true;
    tree_valid=false;assert(!native_usb_ui_bus_device_state(NULL,4,-1,&state));tree_valid=true;
    arena[1024+9]=0;assert(native_usb_ui_bus_device_state(NULL,4,1,&state) && !state.configuration && !state.alternate_known);
    puts("PASS leased native configuration/alternate: identity, complete descriptor boundaries, owner, active pipes, claims and unknown state");
    return 0;
}
