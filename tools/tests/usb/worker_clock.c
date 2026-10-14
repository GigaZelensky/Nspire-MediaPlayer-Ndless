#include "usb_fs_worker.h"
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
static unsigned loops,callbacks,clock_value,mask;
static bool frozen=true;
uint32_t usb_controller_ticks(void) {return frozen ? 0 : ++clock_value;}
unsigned native_critical_enter(void) {unsigned old=mask;mask=0xc0;return old;}
void native_critical_leave(unsigned value) {mask=value;}
unsigned native_interrupt_mask(void) {return mask;}
bool usb_storage_park(void) {return true;}
static void cpu_job(void *unused) {
 (void)unused;
 for(loops=0;loops<100000;++loops) if(!ndvideo_usb_fs_poll()) break;
}
static void cancel_progress(void *unused) {(void)unused;++callbacks;usb_fs_cancel_active();}
int main(void) {
 UsbFsJob job={.run=cpu_job};
 assert(usb_fs_submit(&job));usb_fs_service(8);
 assert(job.pending && loops<=256); /* Foreground regains control without time advancing. */
 usb_fs_set_progress(cancel_progress,NULL);usb_fs_wait(&job);
 assert(job.done && job.error==ECANCELED && callbacks && loops<20000);
 usb_fs_set_progress(NULL,NULL);usb_fs_shutdown();
 frozen=false;loops=0;job=(UsbFsJob){.run=cpu_job};
 assert(usb_fs_submit(&job));usb_fs_wait(&job);
 assert(job.done && !job.error && loops==100000 && mask==0);
 usb_fs_shutdown();puts("PASS stopped-clock CPU yield, foreground cancellation and normal-clock completion");
}
