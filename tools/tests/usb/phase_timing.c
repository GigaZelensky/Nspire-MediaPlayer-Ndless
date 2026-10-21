#include "usb_phase_timing.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static uint32_t clock_ms, clock_reads;
static uint32_t now(void) { ++clock_reads; return clock_ms; }
static char *report(void)
{
    FILE *file=tmpfile();assert(file);
    uint32_t before=clock_reads;
    usb_phase_timing_debug(file);
    assert(clock_reads==before);
    long bytes=ftell(file);assert(bytes>=0 && bytes<32768);
    char *text=calloc((size_t)bytes+1,1);assert(text);
    rewind(file);assert(fread(text,1,(size_t)bytes,file)==(size_t)bytes);fclose(file);
    return text;
}
int main(void)
{
    UsbPhaseTimingStats stats;
    usb_phase_timing_init(now);
#if !NDVIDEO_USB_TIMINGS
    usb_phase_timing_set_enabled(true);
    UsbPhaseTimingToken token=usb_phase_timing_begin(USB_PHASE_MOUNT);
    usb_phase_timing_end(USB_PHASE_MOUNT,token,0);
    usb_phase_timing_directory_end(token,(const char *)(uintptr_t)1,5);
    usb_phase_timing_disk_read_end(token,512,1,0);
    assert(!usb_phase_timing_enabled() && !clock_reads &&
        !usb_phase_timing_snapshot(USB_PHASE_MOUNT,&stats));
    char *text=report();assert(!text[0]);free(text);
    puts("PASS release USB timing is compiled out: no timer reads, records or output");
#else
    assert(usb_phase_timing_enabled());
    clock_ms=100;
    UsbPhaseTimingToken outer=usb_phase_timing_begin(USB_PHASE_OWNERSHIP);
    clock_ms=120;
    UsbPhaseTimingToken inner=usb_phase_timing_begin(USB_PHASE_HOST_WAKE);
    clock_ms=170;usb_phase_timing_end(USB_PHASE_HOST_WAKE,inner,-70);
    clock_ms=200;usb_phase_timing_end(USB_PHASE_OWNERSHIP,outer,0);
    assert(usb_phase_timing_snapshot(USB_PHASE_OWNERSHIP,&stats) && stats.calls==1 &&
        stats.total_ms==100 && stats.min_ms==100 && stats.max_ms==100 && !stats.nonzero_results);
    assert(usb_phase_timing_snapshot(USB_PHASE_HOST_WAKE,&stats) && stats.calls==1 &&
        stats.total_ms==50 && stats.nonzero_results==1 && stats.first_ms==120 && stats.last_ms==170);
    outer=usb_phase_timing_begin(USB_PHASE_MOUNT);
    usb_phase_timing_set_enabled(false);
    uint32_t before=clock_reads;
    usb_phase_timing_end(USB_PHASE_MOUNT,outer,0);
    inner=usb_phase_timing_begin(USB_PHASE_MOUNT);
    usb_phase_timing_end(USB_PHASE_MOUNT,inner,0);
    assert(clock_reads==before && !usb_phase_timing_snapshot(USB_PHASE_MOUNT,&stats));
    usb_phase_timing_directory_end(outer,(const char *)(uintptr_t)1,5);
    assert(clock_reads==before);
    usb_phase_timing_set_enabled(true);
    usb_phase_timing_end(USB_PHASE_MOUNT,outer,0); /* Old generation remains invalid. */
    assert(clock_reads==before);
    outer=usb_phase_timing_begin(USB_PHASE_MOUNT);usb_phase_timing_reset();
    before=clock_reads;usb_phase_timing_end(USB_PHASE_MOUNT,outer,0);assert(clock_reads==before);
    clock_ms=UINT32_MAX-5;
    outer=usb_phase_timing_begin(USB_PHASE_MOUNT);clock_ms=7;
    usb_phase_timing_end(USB_PHASE_MOUNT,outer,0);
    assert(usb_phase_timing_snapshot(USB_PHASE_MOUNT,&stats) && stats.total_ms==13);
    before=clock_reads;outer=usb_phase_timing_begin((enum UsbPhaseTimingPhase)-1);
    usb_phase_timing_end((enum UsbPhaseTimingPhase)USB_PHASE_COUNT,outer,0);
    assert(clock_reads==before && !usb_phase_timing_snapshot(USB_PHASE_COUNT,&stats));
    usb_phase_timing_reset();
    for(unsigned i=0;i<100;++i) {
        clock_ms=1000+i*2;
        outer=usb_phase_timing_begin(USB_PHASE_AUDIO_SETUP);++clock_ms;
        usb_phase_timing_end(USB_PHASE_AUDIO_SETUP,outer,0);
    }
    char *text=report();unsigned rows=0;
    for(char *p=text;(p=strstr(p,"usb_phase_span "))!=NULL;++p)++rows;
    assert(rows==64 && strstr(text,"spans=100 events=100 sampled_out=0 kept=64 omitted=36") &&
        strstr(text,"seq=1 phase=") && strstr(text,"seq=32 phase=") &&
        !strstr(text,"seq=33 phase=") && !strstr(text,"seq=68 phase=") &&
        strstr(text,"seq=69 phase=") && strstr(text,"seq=100 phase="));free(text);
    assert(usb_phase_timing_snapshot(USB_PHASE_AUDIO_SETUP,&stats) && stats.calls==100 && stats.total_ms==100);
    usb_phase_timing_reset();
    for(unsigned i=0;i<100;++i) {
        clock_ms=i*10;outer=usb_phase_timing_begin(USB_PHASE_DIRECTORY_READ);clock_ms+=5;
        usb_phase_timing_end(USB_PHASE_DIRECTORY_READ,outer,0);
    }
    outer=usb_phase_timing_begin(USB_PHASE_DIRECTORY_READ);clock_ms+=9;
    usb_phase_timing_end(USB_PHASE_DIRECTORY_READ,outer,0);
    outer=usb_phase_timing_begin(USB_PHASE_DIRECTORY_READ);clock_ms+=2;
    usb_phase_timing_end(USB_PHASE_DIRECTORY_READ,outer,5);
    text=report();assert(strstr(text,"spans=102 events=3 sampled_out=99 kept=3 omitted=0"));free(text);
    assert(usb_phase_timing_snapshot(USB_PHASE_DIRECTORY_READ,&stats) && stats.calls==102 &&
        stats.total_ms==511 && stats.min_ms==2 && stats.max_ms==9 && stats.nonzero_results==1);
    usb_phase_timing_reset();
    UsbQueueTimingState queue={.flags=USB_QUEUE_VALID|USB_QUEUE_MMIO|USB_QUEUE_ANCHOR_FREE,
        .async_list=0x13f10d80,.free_head=0x13f10d80};
    UsbQueueTimingPrivate private_state={.token=0x001f8c80,.native_result=12};
    for(unsigned i=0;i<30;++i) {clock_ms=i;usb_phase_timing_queue_state("test_boundary",&queue,&private_state);}
    text=report();rows=0;
    for(char *p=text;(p=strstr(p,"usb_queue_state "))!=NULL;++p)++rows;
    assert(rows==16 && strstr(text,"usb_queue_retention seen=30 kept=16 omitted=14") &&
        strstr(text,"seq=8 stage=") && !strstr(text,"seq=9 stage=") &&
        !strstr(text,"seq=22 stage=") && strstr(text,"seq=23 stage=") && strstr(text,"seq=30 stage=") &&
        strstr(text,"async=13f10d80") && strstr(text,"token=001f8c80") && strstr(text,"native_result=12"));free(text);
    usb_phase_timing_reset();
    private_state.error=0;
    usb_phase_timing_queue_state("private_transfer_failed",&queue,&private_state);
    text=report();assert(!strstr(text,"usb_queue_first_failure"));free(text);
    for(unsigned i=1;i<50;++i) {
        private_state.error=i==10?4:0;
        private_state.token=i==10?0x801f8c80:0;
        if(i==20)private_state.error=5; /* Later failure must not replace first. */
        usb_phase_timing_queue_state("private_transfer_failed",&queue,&private_state);
    }
    text=report();assert(strstr(text,"usb_queue_first_failure seq=11 retained=1\n") &&
        strstr(text,"usb_queue_private seq=11 phase=0 error=4") &&
        strstr(text,"token=801f8c80") && !strstr(text,"usb_queue_state seq=21 stage="));free(text);
    usb_phase_timing_reset();
    char path[]="0:/private-parent/nMedia/Music/Song\" art\n";
    clock_ms=10;outer=usb_phase_timing_begin(USB_PHASE_DIRECTORY_OPEN);clock_ms=35;
    usb_phase_timing_directory_end(outer,path,5);
    memset(path,'X',sizeof(path)-1); /* Recorder owns its copy. */
    text=report();assert(strstr(text,"phase=directory_open start_ms=10 elapsed_ms=25 result=5 path_hash=") &&
        strstr(text,"leaf=Song__art_") && !strstr(text,"private-parent") && !strstr(text,"Music/"));free(text);
    assert(usb_phase_timing_snapshot(USB_PHASE_DIRECTORY_OPEN,&stats) && stats.calls==1 &&
        stats.total_ms==25 && stats.nonzero_results==1);
    char bounded[1024];memset(bounded,'q',sizeof(bounded));
    outer=usb_phase_timing_begin(USB_PHASE_DIRECTORY_OPEN);
    usb_phase_timing_directory_end(outer,bounded,0); /* No terminator, still bounded. */
    outer=usb_phase_timing_begin(USB_PHASE_DIRECTORY_OPEN);
    usb_phase_timing_directory_end(outer,"0:/",0);
    text=report();assert(strstr(text,"leaf=qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq\n") &&
        strstr(text,"leaf=-\n"));free(text);
    outer=usb_phase_timing_begin(USB_PHASE_DIRECTORY_OPEN);usb_phase_timing_reset();
    before=clock_reads;
    usb_phase_timing_directory_end(outer,(const char *)(uintptr_t)1,0);
    assert(clock_reads==before);
    usb_phase_timing_reset();
    outer=usb_phase_timing_begin(USB_PHASE_DISK_READ);clock_ms+=3;
    usb_phase_timing_disk_read_end(outer,512,2,0);
    outer=usb_phase_timing_begin(USB_PHASE_DISK_READ);clock_ms+=7;
    usb_phase_timing_disk_read_end(outer,512,3,1);
    text=report();assert(strstr(text,"usb_disk_reads calls=2 requested_sectors=5 completed_sectors=2 completed_bytes=1024") &&
        strstr(text,"phase=disk_read calls=2 nonzero=1 total_ms=10"));free(text);
    assert(usb_phase_timing_snapshot(USB_PHASE_DISK_READ,&stats) && stats.calls==2 && stats.total_ms==10);
    usb_phase_timing_reset();
    for(unsigned i=0;i<1000;++i) {
        clock_ms=i*32;outer=usb_phase_timing_begin(USB_PHASE_RESOLVE);clock_ms+=4;
        usb_phase_timing_end(USB_PHASE_RESOLVE,outer,0);
    }
    outer=usb_phase_timing_begin(USB_PHASE_MOUNT);clock_ms+=12;usb_phase_timing_end(USB_PHASE_MOUNT,outer,0);
    text=report();assert(strstr(text,"events=2 sampled_out=999 kept=2") && strstr(text,"seq=1001 phase=mount"));free(text);
    usb_phase_timing_init(NULL);usb_phase_timing_set_enabled(true);
    assert(!usb_phase_timing_enabled());
    puts("PASS USB phase timing: injected clock, inclusive nesting, wraparound, disabled/reset tokens, bounded first/latest events and sampled repetitive waits/reads");
#endif
    return 0;
}
