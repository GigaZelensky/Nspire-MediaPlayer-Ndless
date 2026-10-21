#include "usb_controller.h"
#include "usb_storage.h"
#include "usb_fs_worker.h"
#include "usb_phase_timing.h"
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include <assert.h>
#include <errno.h>
#include <string.h>

static uint32_t clock_ticks, irq_mask;
static unsigned step_ticks, polls, finish_after, audio_pumps, begin_calls, unavailable;
static unsigned cancel_after;
static UsbCommand *owner;
static UsbFsJob *active_job;
static void *destination;
static bool legacy;
uint32_t usb_controller_ticks(void) { return clock_ticks; }
unsigned native_critical_enter(void) { unsigned old=irq_mask;irq_mask=0xc0;return old; }
void native_critical_leave(unsigned mask) { irq_mask=mask; }
unsigned native_interrupt_mask(void) { return irq_mask; }
bool usb_controller_online(void) { return true; }
uint32_t usb_controller_generation(void) { return 1; }
uint32_t usb_controller_sector_size(void) { return 512; }
uint64_t usb_controller_last_sector(void) { return 1023; }
unsigned usb_controller_max_read_bytes(void) { return USB_READ_BYTES; }
bool usb_storage_park(void) { assert(!owner);return true; }
bool usb_controller_begin_read(UsbCommand *request,uint64_t sector,unsigned count,void *data)
{
    assert(irq_mask==0xc0 && !owner && sector==3 && count==1);
    ++begin_calls;
    if (unavailable) { --unavailable;return false; }
    owner=request;request->pending=true;destination=data;return true;
}
int usb_controller_step(UsbCommand *request)
{
    assert(irq_mask==0xc0 && owner==request && request->pending);
    ++polls;++audio_pumps;clock_ticks+=step_ticks;
    if (cancel_after && polls==cancel_after) usb_fs_cancel(active_job);
    if (polls<finish_after) return 0;
    memset(destination,0xa5,512);request->pending=false;owner=NULL;return 1;
}

/* PRODUCTION_DISK_READ */
/* LEGACY_DISK_READ */
typedef struct { UsbFsJob job; DRESULT result; BYTE data[512]; } ReadJob;
static void read_job(void *argument)
{
    ReadJob *job=argument;
    job->result=legacy?legacy_disk_read(0,job->data,3,1):disk_read(0,job->data,3,1);
}
static unsigned run(bool old,unsigned tick_step,unsigned complete,unsigned cancel,unsigned busy)
{
    usb_fs_shutdown();
    clock_ticks=irq_mask=polls=audio_pumps=begin_calls=0;
    owner=NULL;step_ticks=tick_step;finish_after=complete;cancel_after=cancel;unavailable=busy;legacy=old;
    ReadJob job={0};memset(job.data,0xcc,sizeof(job.data));
    job.job.run=read_job;job.job.argument=&job;active_job=&job.job;
    assert(usb_fs_submit(&job.job));
    unsigned turns=0;
    while(job.job.pending) {
        unsigned before=polls;
        usb_fs_service(8);
        assert(++turns<10000 && irq_mask==0);
        /* Tick budget and the independent stopped-timer CPU bound both leave
         * foreground turns available; pending DMA retains its stack owner. */
        assert(polls-before<=(old?1U:tick_step?8U:256U));
        if (polls && polls<complete) {
            assert(owner && job.job.pending);
            for(unsigned i=0;i<sizeof(job.data);++i)assert(job.data[i]==0xcc);
        }
        if(tick_step)clock_ticks+=524; /* Fixed16ms picker/input cadence. */
    }
    assert(job.job.done && !owner && polls==complete && audio_pumps==complete && begin_calls==busy+1);
    assert(job.job.error==(cancel?ECANCELED:0) && job.result==(cancel?RES_ERROR:RES_OK));
    for(unsigned i=0;i<sizeof(job.data);++i)assert(job.data[i]==0xa5);
    usb_fs_shutdown();return turns;
}
int main(void)
{
    unsigned old=run(true,1,48,0,0),current=run(false,1,48,0,0);
    assert(old>=48 && current<=7 && current*6<old);
    unsigned stopped_old=run(true,0,3000,0,0),stopped=run(false,0,3000,0,0);
    assert(stopped_old>=3000 && stopped<=12);
    assert(run(false,1,48,3,0)<=7); /* Cancellation drains, never frees active DMA. */
    assert(run(false,0,600,1,0)<=3);
    assert(run(false,1,1,0,2)==3); /* Unavailable begin still yields each turn. */
    printf("PASS read scheduling:48polls %u -> %u foreground turns; stopped timer3000polls %u -> %u; cancellation drains, DMA owner/input/audio pumps preserved\n",
        old,current,stopped_old,stopped);
    return 0;
}
