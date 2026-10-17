#define _GNU_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
typedef struct {uint32_t bus,hub_owner;} UsbHostTree;
static struct {bool classic_cx;} api;
static uint8_t *arena;
static bool layout_ok=true;
#define WORD(at) (*(uint32_t *)(uintptr_t)(at))
static bool memory(uint32_t at,unsigned size)
{return at>=(uintptr_t)arena && at<=(uintptr_t)arena+4096-size;}
static bool route_layout_valid(void) {return layout_ok;}
/* PRODUCTION_PARENT */
int main(void)
{
#ifdef _WIN32
    arena=VirtualAlloc((void *)(uintptr_t)0x11000000,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    arena=mmap((void *)(uintptr_t)0x11000000,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(arena==MAP_FAILED)arena=NULL;
#endif
    assert(arena && (uintptr_t)arena<=UINT32_MAX-4096);
    uint32_t child=(uint32_t)(uintptr_t)arena,parent=child+256,port=child+512,owner=child+768;
    UsbHostTree tree={child+1024,child+1280};
    arena[10]=1;assert(storage_parent_owner(&tree,child)==tree.hub_owner);
    arena[10]=3;WORD(child+24)=parent;WORD(child+20)=port;
    arena[28]=1;arena[256+8]=2;WORD(tree.bus+52+2*4)=parent;
    WORD(parent)=tree.bus;WORD(port+8)=child;WORD(port+12)=parent;
    arena[256+64]=18;arena[256+65]=1;arena[256+68]=9;arena[256+10]=2;
    WORD(parent+100)=owner;WORD(owner+16)=parent;
    assert(storage_parent_owner(&tree,child)==owner);
    layout_ok=false;assert(!storage_parent_owner(&tree,child));layout_ok=true;
    api.classic_cx=true;assert(!storage_parent_owner(&tree,child));api.classic_cx=false;
    WORD(port+8)^=4;assert(!storage_parent_owner(&tree,child));WORD(port+8)^=4;
    WORD(parent)^=4;assert(!storage_parent_owner(&tree,child));WORD(parent)^=4;
    WORD(owner+16)^=4;assert(!storage_parent_owner(&tree,child));WORD(owner+16)^=4;
    arena[256+68]=1;assert(!storage_parent_owner(&tree,child));arena[256+68]=9;
    arena[10]=4;assert(!storage_parent_owner(&tree,child));
    puts("PASS authenticated nested-hub storage parent: layout, model, links, bus, owner, class and depth guards");
    return 0;
}
