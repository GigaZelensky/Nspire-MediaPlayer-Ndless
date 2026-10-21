#include "player/header_diagnostic.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if NDMEDIA_HEADER_DIAGNOSTIC
#include <os.h>
#include "player/player_data.h"
struct NUC_FILE {unsigned value;};
static struct NUC_FILE native;
static unsigned mask=0x80,parked,native_reads,opens,mutations,native_fail;
unsigned nl_hwtype(void){return 1;}
unsigned nl_hwsubtype(void){return 2;}
unsigned nl_ndless_rev(void){return 2022;}
unsigned native_interrupt_mask(void){return mask;}
void native_critical_leave(unsigned saved){mask=saved;}
void raw_player_before_native(void){++parked;}
void storage_mutation_invalidate(void){++mutations;}
NUC_FILE *nuc_fopen(const char *path,const char *mode)
{assert(parked && mask==0x80 && !strcmp(mode,"rb") && !strncmp(path,"/media/",7));++opens;return native_fail?NULL:&native;}
size_t nuc_fread(void *p,size_t size,size_t n,NUC_FILE *f)
{assert(f==&native && size==1 && n==56);memset(p,0x31,n);++native_reads;return n;}
int nuc_fseek(NUC_FILE *f,long p,int mode){assert(f==&native && !p && mode==SEEK_END);return 0;}
long nuc_ftell(NUC_FILE *f){assert(f==&native);return 120000000;}
int nuc_fclose(NUC_FILE *f){assert(f==&native);return 0;}
void performance_clock_debug(FILE *f){fputs("test_clock=396\n",f);}
static void save_error(void)
{
    FILE *f=player_error_log_open("ndvideo-error.log.tns",true);assert(f);
    fputs("nMedia Player diagnostic log\nlast_error=open failed: header read\n",f);
    header_diagnostic_write(f);
    assert(!ferror(f));assert(!fclose(f));
}
static char *report(void)
{
    FILE *f=fopen("ndvideo-error.log.tns","rb");assert(f);assert(!fseek(f,0,SEEK_END));
    long n=ftell(f);assert(n>0 && n<20000);rewind(f);char *p=calloc((size_t)n+1,1);assert(p);
    assert(fread(p,1,(size_t)n,f)==(size_t)n);fclose(f);return p;
}
#endif
int main(void)
{
#if !NDMEDIA_HEADER_DIAGNOSTIC
    header_diagnostic_begin("ignored");header_diagnostic_result("ignored",0,false,NULL,0,0,0,false);
    header_diagnostic_compare("ignored");header_diagnostic_write(NULL);
    FILE *f=fopen("ndvideo-error.log.tns","rb");assert(!f);
    puts("PASS production header diagnostic disabled: no allocation, storage access or report");
#else
    static PortableReaderPlatform p;static PortableStorageSnapshot s;static RawFileReader r;
    p.asic=0x202;p.kind=NAND_PAGE_CX2_SPI;s.file_bytes=120000000;s.inode=123;s.tag_column=2052;
    memset(s.state_data,0x42,sizeof(s.state_data));memset(s.meta,0x43,sizeof(s.meta));
    r.initialized=true;r.io=r.io_storage;r.bytes=2112;r.error=14;r.source=1;
    r.loader.id=6;r.loader.phase=1;r.loader.error=7;r.loader.count=1;r.loader.units[0].unit=13;
    memset(r.io,0x55,2112);
    header_diagnostic_begin("/media/backrooms.nvp.tns");
    header_diagnostic_storage(&p,&s,&r,0,1024,-1,4,true,false);
    /* Cleanup/another operation can overwrite all live scratch memory. */
    memset(s.state_data,0,sizeof(s.state_data));memset(r.io,0,2112);r.loader.error=0;r.error=0;
    header_diagnostic_storage(&p,&s,&r,0,1024,1024,4,true,true);
    header_diagnostic_result("header read",120000000,false,NULL,0,48,5,false);
    assert(!native_reads && !opens);
    header_diagnostic_compare("/media/another.nvp.tns");assert(!opens);
    header_diagnostic_compare("/media/backrooms.nvp.tns");assert(native_reads==1 && opens==1 && mutations==2 && mask==0x80);
    save_error(); /* The ordinary error report now owns the complete capture. */
    char *text=report();assert(strstr(text,"stage=header read expected=48 received=0 errno=5"));
    assert(strstr(text,"error=14 (FlashFX region supply rejected)") && strstr(text,"region id=6 phase=1 error=7"));
    assert(strstr(text,"nMedia Player diagnostic log") && strstr(text,"nMedia header diagnostic v2"));
    assert(strstr(text,"4242424242424242") && strstr(text,"5555555555555555") && strstr(text,"size=120000000 close=0"));free(text);
    header_diagnostic_compare("/media/backrooms.nvp.tns");assert(native_reads==1); /* one comparison per attempt */
    assert(!fopen("ndmedia-header.log.tns","rb"));
    remove("ndvideo-error.log.tns");
    header_diagnostic_begin("/media/protected.nvp.tns");
    header_diagnostic_result("invalid header",128,true,NULL,0,48,5,false);
    header_diagnostic_compare("/media/protected.nvp.tns");save_error();
    assert(native_reads==1);text=report();assert(strstr(text,"encrypted=1") && strstr(text,"attempted=0") && !strstr(text,"FlashFX_state"));free(text);
    header_diagnostic_begin("usb:/1234/video.nvp");
    header_diagnostic_result("header valid",100,false,(const uint8_t *)"NVP0",4,4,0,true);
    header_diagnostic_compare("usb:/1234/video.nvp");save_error();
    text=report();assert(strstr(text,"valid_header=1") && strstr(text,"last_attempt=3") && strstr(text,"attempted=0"));free(text);
    assert(native_reads==1);
    /* The primary capture remains useful if native open fails, without an
     * invented success status or waiting for app shutdown. */
    remove("ndvideo-error.log.tns");native_fail=1;
    header_diagnostic_begin("/media/missing.nvp.tns");
    header_diagnostic_result("file open",0,false,NULL,0,48,5,false);
    header_diagnostic_compare("/media/missing.nvp.tns");save_error();
    text=report();assert(strstr(text,"completed=1 attempted=1 opened=0 received=0 seek_end=-1 size=-1 close=-1"));free(text);
    assert(mask==0x80);
    /* Repeated failures use the real error-log rotation, not a new side file. */
    FILE *old=fopen("ndvideo-error.log.tns","wb");assert(old);
    for(unsigned i=0;i<65536;++i)fputc('X',old);
    assert(!fclose(old));save_error();text=report();assert(!strncmp(text,"nMedia Player diagnostic log",27));free(text);
    puts("PASS header probe: owned snapshot survives cleanup, comparison once after failed movie closes, full capture in real bounded error report before app exit, encrypted/USB skip, native-open failure and exact mask preservation");
#endif
    return 0;
}
