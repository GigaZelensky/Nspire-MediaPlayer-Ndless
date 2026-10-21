#include "usb_phase_timing.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
typedef int FRESULT;
enum { FR_OK, FR_NO_PATH, FR_NO_FILE, FR_DISK_ERR };
typedef struct { int error; } UsbFsJob;
typedef struct { UsbFsJob job; unsigned long serial; } MountJob;
typedef struct { int unused; } DIR;
static int volume, mount_result, serial_result, first_root_result, legacy_result;
static unsigned mounts, serials, roots, closes;
static uint32_t clock_ms, clock_reads;
static const char *media_folder="nMedia";
static uint32_t now(void) { ++clock_reads;return clock_ms; }
static int failure(FRESULT r) { return r==FR_OK?0:r==FR_DISK_ERR?EIO:ENOENT; }
static FRESULT f_mount(int *v,const char *path,int immediate)
{
    assert(v==&volume && !strcmp(path,"0:") && immediate==1);
    ++mounts;clock_ms+=59;return mount_result;
}
static FRESULT f_getlabel(const char *path,void *label,unsigned long *serial)
{
    assert(!strcmp(path,"0:") && !label && serial);
    ++serials;clock_ms+=3;*serial=0x1234;return serial_result;
}
static FRESULT f_opendir(DIR *dir,const char *path)
{
    assert(dir);
    if (!roots++) {assert(!strcmp(path,"0:/nMedia"));clock_ms+=11;return first_root_result;}
    assert(!strcmp(path,"0:/ndvideo"));clock_ms+=17;return legacy_result;
}
static void f_closedir(DIR *dir) { assert(dir);++closes;clock_ms+=2; }
/* PRODUCTION_MOUNT_JOB */
static void run(int fs,int serial,int root,int legacy)
{
    usb_phase_timing_init(now);
    mount_result=fs;serial_result=serial;first_root_result=root;legacy_result=legacy;
    mounts=serials=roots=closes=clock_ms=clock_reads=0;
    MountJob job={0};mount_job(&job);
    assert(mounts==1 && serials==(unsigned)(fs==FR_OK) &&
        roots==(unsigned)(fs==FR_OK && serial==FR_OK ?
            (root==FR_NO_FILE || root==FR_NO_PATH ? 2:1):0));
    assert(closes==(unsigned)(fs==FR_OK && serial==FR_OK &&
        (root==FR_OK || ((root==FR_NO_FILE || root==FR_NO_PATH) && legacy==FR_OK))));
    int expected=fs?fs:serial?serial:root==FR_NO_FILE || root==FR_NO_PATH?legacy:root;
    assert(job.job.error==failure(expected));
    UsbPhaseTimingStats stats;
#if NDVIDEO_USB_TIMINGS
    assert(usb_phase_timing_snapshot(USB_PHASE_MOUNT_FS,&stats) && stats.calls==1 &&
        stats.total_ms==59 && stats.nonzero_results==(unsigned)(fs!=0));
    bool recorded=usb_phase_timing_snapshot(USB_PHASE_MOUNT_SERIAL,&stats);
    assert(recorded==(fs==FR_OK));
    if(recorded)assert(stats.total_ms==3 && stats.nonzero_results==(unsigned)(serial!=0));
    recorded=usb_phase_timing_snapshot(USB_PHASE_MOUNT_ROOT,&stats);
    assert(recorded==(fs==FR_OK && serial==FR_OK));
    if(recorded)assert(stats.total_ms==11U+(roots==2?17U:0U)+(closes?2U:0U) &&
        stats.nonzero_results==(unsigned)(expected!=0));
#else
    assert(!clock_reads && !usb_phase_timing_snapshot(USB_PHASE_MOUNT_FS,&stats));
#endif
}
int main(void)
{
    run(FR_OK,FR_OK,FR_OK,FR_OK);assert(!strcmp(media_folder,"nMedia"));
    run(FR_OK,FR_OK,FR_NO_PATH,FR_OK);assert(!strcmp(media_folder,"ndvideo"));
    run(FR_OK,FR_OK,FR_NO_FILE,FR_DISK_ERR);
    run(FR_OK,FR_OK,FR_DISK_ERR,FR_OK);
    run(FR_OK,FR_DISK_ERR,FR_OK,FR_OK);
    run(FR_DISK_ERR,FR_OK,FR_OK,FR_OK);
    puts("PASS mount subphase timing: unchanged native/legacy paths and early failures; default-off clock silence");
    return 0;
}
