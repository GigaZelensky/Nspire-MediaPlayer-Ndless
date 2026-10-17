/* Actual production control/discovery/cleanup with a bounded fake EHCI/TT.
 * Suspended descendants don't answer control requests until their hub port
 * is resumed. This models that contract, not electrical hardware timing. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#define NDVIDEO_USB_AUDIO_TEST
#ifndef NDVIDEO_USB_AUDIO_DRIVER
#define NDVIDEO_USB_AUDIO_DRIVER "../../../src/audio/usb_audio.c"
#endif
#include NDVIDEO_USB_AUDIO_DRIVER

volatile uint32_t usb_test_registers[64];
volatile uint32_t usb_test_irq_enable, usb_test_irq_disable;
unsigned usb_test_cx2=1;
static void *allocation;
static size_t allocation_bytes;
static unsigned clock_ms, scenario, resumes, suspends, alternate, rate, configuration;
static unsigned state_reads;
static unsigned alternate_queries;
static unsigned hub_address[2], link_port[2], leaf_address, hubs;
static bool suspended[2], originally_suspended[2], fail_resume, cancel_after_resume;
static bool resume_pending[2], status_failed_once;
static uint32_t resume_at[2];
static const uint8_t config[]={
    9,2,61,0,2,1,0,0x80,50,
    9,4,1,0,0,1,2,0,0,
    9,4,1,1,1,1,2,0,0,
    7,0x24,1,1,1,1,0,
    11,0x24,2,1,2,2,16,1,0x80,0xbb,0,
    9,5,1,9,192,0,1,0,0,
    7,0x25,1,1,0,0,0
};
static void check(bool ok,const char *message)
{
    if(!ok) {fprintf(stderr,"hub case%u: %s\n",scenario,message);exit(1);}
}
void *usb_test_allocate(size_t bytes)
{
    allocation_bytes=(bytes+4095)&~(size_t)4095;
#ifdef _WIN32
    allocation=VirtualAlloc((void *)0x11000000,allocation_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    int flags=MAP_PRIVATE|MAP_ANONYMOUS;
#ifdef MAP_FIXED_NOREPLACE
    flags|=MAP_FIXED_NOREPLACE;
#endif
    allocation=mmap((void *)0x11000000,allocation_bytes,PROT_READ|PROT_WRITE,
                    flags,-1,0);
    if(allocation==MAP_FAILED)allocation=NULL;
    if(allocation && allocation!=(void *)0x11000000) {
        munmap(allocation,allocation_bytes);allocation=NULL;
    }
#endif
    if(scenario==27)usb_test_registers[4]|=64U;
    if(scenario==28)usb_test_registers[4]|=2U;
    if(scenario==29)usb_test_registers[12]&=~1U;
    return allocation;
}
void usb_test_free(void *p)
{
    check(p==allocation,"wrong DMA free");
#ifdef _WIN32
    VirtualFree(p,0,MEM_RELEASE);
#else
    munmap(p,allocation_bytes);
#endif
    allocation=NULL;
}
static bool dma(uint32_t address,size_t bytes)
{
    return allocation && address>=0x11000000U && bytes<=allocation_bytes &&
           address-0x11000000U<=allocation_bytes-bytes;
}
static int hub_index(unsigned address)
{
    for(unsigned i=0;i<hubs;++i)if(address==hub_address[i])return (int)i;
    return -1;
}
void usb_test_poll(void)
{
    for (unsigned i=0;i<hubs;++i) {
        if (resume_pending[i] && scenario!=8 && (int32_t)(clock_ms-resume_at[i])>=0) {
            suspended[i]=false;resume_pending[i]=false;
        }
    }
    uint32_t command=usb_test_registers[4];
    usb_test_registers[5]&=~(ASS|PSS|HALTED);
    if(!(command&RUN)){usb_test_registers[5]|=HALTED;return;}
    if(command&ASE)usb_test_registers[5]|=ASS;
    if(command&PSE)usb_test_registers[5]|=PSS;
    if(!(command&ASE) || !dma(usb_test_registers[10],sizeof(Qh)))return;
    Qh *qh=(Qh *)(uintptr_t)usb_test_registers[10];
    unsigned address=qh->info1&127;
    int hub=hub_index(address);
    bool leaf=address==leaf_address;
    if(!leaf && hub<0)return;
    if((leaf && (suspended[0] || (hubs==2 && suspended[1]))) ||
       (hub==1 && suspended[0]))return;
    check(((qh->info1>>12)&3)==(leaf?0U:2U),"wrong speed");
    if(leaf) {
        check(((qh->info2>>16)&127)==hub_address[hubs-1] &&
              ((qh->info2>>23)&127)==link_port[hubs-1],"wrong translator route");
        check(qh->info2&(1U<<11),"missing complete split");
    }
    check(dma(qh->overlay.next,3*sizeof(Qtd)),"control descriptors outside DMA");
    Qtd *q=(Qtd *)(uintptr_t)qh->overlay.next;
    if(!(q->token&0x80))return;
    uint8_t *setup=(uint8_t *)(uintptr_t)q->buffer[0];
    unsigned length=get16(setup+6),request=setup[1],value=get16(setup+2),index=get16(setup+4);
    Qtd *data=(Qtd *)(uintptr_t)q->next;
    Qtd *status=length?(Qtd *)(uintptr_t)data->next:data;
    uint8_t *payload=(uint8_t *)(uintptr_t)data->buffer[0];
    bool ok=true;
    if(setup[0]==0xa0 && request==6 && value==0x2900) {
        check(!leaf && (length==7 || length==9),"bad hub descriptor request");
        const uint8_t descriptor[]={9,0x29,7,9,0,50,0,0,0xff};
        memcpy(payload,descriptor,length);
    } else if(setup[0]==0xa3 && request==0 && value==0) {
        check(!leaf && length==4 && index>=1 && index<=7,"bad hub status request");
        put32(payload,index==link_port[hub]?
              ((hub==(int)hubs-1?0x103U:0x503U)|(suspended[hub]?4U:0U)):0x100U);
        if(scenario==9 && hub==0 && index==link_port[hub] && resumes && !status_failed_once) {
            status_failed_once=true;ok=false;
        }
    } else if(setup[0]==0x23 && value==2 && !length && (request==1 || request==3)) {
        check(!leaf && index==link_port[hub],"changed unrelated port");
        if(request==1) {
            ++resumes;
            if(fail_resume && hub==0)ok=false;
            else if(scenario>=6 && scenario<=8) {
                resume_pending[hub]=true;
                resume_at[hub]=clock_ms+(scenario==7?25:70);
            } else {
                suspended[hub]=false;
                if(scenario==10 && hub==0)ok=false; /* Write applied; status stage failed. */
            }
        } else {
            check(hub!=0 || hubs==1 || !originally_suspended[1] || suspended[1],
                  "parent suspended before descendant restore");
            ++suspends;suspended[hub]=true;
        }
    } else if(setup[0]==0x23 && request==8 && !length) {
        check(!leaf,"TT clear sent to audio endpoint");
    } else if(request==6 && value==0x100 && setup[0]==0x80) {
        uint8_t descriptor[]={18,1,0,2,0,0,0,64,0xac,5,0x0b,0x11,0,1,0,0,0,1};
        if(!leaf){descriptor[4]=9;descriptor[6]=scenario%2?2:1;}
        check(length==8 || length==18,"bad device descriptor request");
        memcpy(payload,descriptor,length);
        if(leaf && scenario==11)ok=false;
    } else if(request==6 && value==0x200 && setup[0]==0x80) {
        check(length<=sizeof(config),"bad configuration size");memcpy(payload,config,length);
        if(leaf && scenario==12 && length>32)payload[32]=3; /* IEEE float, no supported PCM. */
    } else if(request==8 && setup[0]==0x80) {
        check(leaf && length==1,"GET_CONFIGURATION recipient");payload[0]=(uint8_t)configuration;
        if(scenario==14 || scenario==15 || scenario==17 || scenario==21 || scenario==22 || scenario==24)ok=false;
        if(scenario==16)return; /* No completion: timeout must not use native state. */
    } else if(request==9 && setup[0]==0) {
        check(leaf && !length && (value==0 || value==1),"SET_CONFIGURATION recipient/value");
        configuration=value;alternate=0;
    } else if(request==10 && setup[0]==0x81) {
        ++alternate_queries;
        check(leaf && index==1 && length==1,"GET_INTERFACE recipient");payload[0]=(uint8_t)alternate;
        if(scenario==13 || (scenario>=18 && scenario<=21) || scenario==24 || scenario==25)ok=false;
        if(scenario==23 || scenario==26)return;
    } else if(request==11 && setup[0]==1) {
        check(leaf && index==1 && !length,"SET_INTERFACE recipient");alternate=value;
    } else if((setup[0]==0xa2 || setup[0]==0x22) && value==0x100 && length==3) {
        check(leaf,"sample rate recipient");
        if(request==0x81) {payload[0]=(uint8_t)rate;payload[1]=(uint8_t)(rate>>8);payload[2]=(uint8_t)(rate>>16);}
        else {check(request==1,"unknown audio request");rate=payload[0]|payload[1]<<8|payload[2]<<16;}
    } else check(false,"unexpected device reset/address/power/control write");
    q->token&=~0x80U;
    if(!ok){data->token=(data->token&~0x80U)|0x40U;return;}
    if(length)data->token&=~(0x80U|0x7fff0000U);
    status->token&=~0x80U;
}
static uint32_t now(void)
{
    ++clock_ms;usb_test_registers[7]=(usb_test_registers[7]+8)&0x3fff;
    usb_test_poll();return clock_ms;
}
static bool progress(void *unused)
{
    (void)unused;return !cancel_after_resume || !resumes;
}
static bool lookup(void *unused,uint8_t address,UsbAudioRoute *out)
{
    (void)unused;int hub=hub_index(address);
    if(hub>=0) {
        *out=(UsbAudioRoute){address,3,hub?hub_address[0]:1,hub?link_port[0]:1,0,0,false};
        return true;
    }
    if(address!=leaf_address)return false;
    *out=(UsbAudioRoute){address,2,hub_address[hubs-1],link_port[hubs-1],
                        hub_address[hubs-1],link_port[hubs-1],scenario%2!=0};
    return true;
}
static bool device_state(void *unused,uint8_t address,int iface,UsbDeviceState *out)
{
    (void)unused;check(address==leaf_address,"native state addressed wrong device");++state_reads;
    if(scenario==15 || scenario==20)return false;
    *out=(UsbDeviceState){.configuration=(uint8_t)(scenario==22 || scenario==25?2:scenario==17 || scenario==24?0:1),
                         .alternate=(uint8_t)(scenario==19?1:0),.alternate_known=iface==1 && scenario!=26};
    return true;
}
static void reset(unsigned mode)
{
    scenario=mode;clock_ms=resumes=suspends=alternate=state_reads=alternate_queries=0;rate=44100;
    configuration=mode==17 || mode==24?0:1;
    memset(resume_pending,0,sizeof(resume_pending));status_failed_once=false;
    hubs=mode==0?1:2;
    hub_address[0]=mode%2?73:2;hub_address[1]=mode%2?7:100;
    link_port[0]=mode%2?4:1;link_port[1]=mode%2?2:7;
    leaf_address=mode==0?6:3;
    if(mode==0)link_port[0]=4;
    suspended[0]=originally_suspended[0]=mode!=3;
    suspended[1]=originally_suspended[1]=hubs==2 && mode!=3;
    fail_resume=mode==4;cancel_after_resume=mode==5;
    memset((void *)usb_test_registers,0,sizeof(usb_test_registers));
    usb_test_registers[0]=0x01000010;usb_test_registers[4]=RUN|ASE|PSE;
    usb_test_registers[6]=0x27;usb_test_registers[9]=0x12000000;
    usb_test_registers[10]=0x12001000;usb_test_registers[12]=5;
    usb_test_registers[32]=2U<<22;usb_test_irq_enable=IRQ_USB|3;
    usb_audio_set_route_lookup(lookup,NULL);usb_audio_set_progress(progress,NULL);
    usb_audio_set_state_lookup(mode>=14?device_state:NULL,NULL);
    usb_audio_probe_only(false);
}
int main(void)
{
    check(volume_gain_q15(0)==0 && volume_gain_q15(100)==32768 && volume_gain_q15(150)==32768,
        "volume endpoints or upper clamp changed");
    check(volume_gain_q15(5)==82 && volume_gain_q15(50)==8192,"quiet volume taper missing");
    for(unsigned p=1;p<=100;++p)
        check(volume_gain_q15(p)>volume_gain_q15(p-1),"volume curve is not monotonic");
    for(int sample=-32768;sample<=32767;++sample) {
        check(pcm_gain(sample,32768)==sample,"100 percent is not bit exact");
        check(!pcm_gain(sample,0),"zero volume is not silent");
    }
    puts("PASS perceptual volume curve: finer low gain, monotonic controls, exact mute and bit-perfect unity maximum");
    for(unsigned mode=27;mode<=29;++mode) {
        reset(mode);usb_test_irq_disable=0;
        const char *message=NULL;
        check(!usb_audio_open(now,&message) && !allocation,"native state race was stolen");
        check(strstr(message,"settling") && !usb_test_irq_disable && usb_test_registers[6]==0x27,
            "refused takeover changed interrupts");
        check(usb_test_registers[4]==((RUN|ASE|PSE)|(mode==27?64U:mode==28?2U:0)),
            "refused takeover changed native command");
    }
    puts("PASS native reset/advance/disconnect after allocation refuses takeover without touching native registers");
    for(unsigned mode=0;mode<27;++mode) {
      reset(mode);
      for(unsigned repeat=0;repeat<2;++repeat) {
        /* Reopen against the controller/device state left by cleanup. */
        resumes=suspends=0;
        status_failed_once=false;
        const char *message=NULL;
        UsbAudio *u=usb_audio_open(now,&message);
        bool expected=mode!=4 && mode!=5 && mode!=8 &&
            (mode<11 || mode==14 || mode==17 || mode==18 || mode==21 || mode==23 || mode==24);
        if(!expected)check(!u,"failed/cancelled discovery started playback");
        else {
            check(u && !u->unsafe_dma,message?message:"audio open failed");
            check(u->address==leaf_address && u->tt_hub==hub_address[hubs-1],"wrong audio route");
            check(resumes==(mode==3?0U:hubs),"missing/unnecessary hub wake");
            int16_t pcm[20000*2];
            for(unsigned i=0;i<20000;++i){pcm[i*2]=16000;pcm[i*2+1]=-16000;}
            AudioBuffer buffer={.pcm=pcm,.sample_count=20000,.sample_rate=48000,.channels=2};
            check(usb_audio_start(u,&buffer,0,1,1,20),"playback schedule failed");
            check(get16(u->packets)==640 && (int16_t)get16(u->packets+2)==-640,
                "USB packet samples did not use the perceptual volume curve");
            usb_audio_volume(u,100);check(u->gain_q15==32768,"live volume update lost unity maximum");
            check(u->running && (usb_test_registers[4]&PSE),"playback not enabled");
            check(usb_audio_pause(u),"pause failed");
            check(usb_audio_start(u,&buffer,usb_audio_position(u),1,1,20),"retained resume failed");
            check(usb_audio_close(u),"audio cleanup failed");
        }
        check(!allocation && !topology_restore_errors,"DMA or port restoration failure");
        for(unsigned i=0;i<hubs;++i)check(suspended[i]==originally_suspended[i],"port state changed on exit");
        check(!alternate && rate==44100,"audio controls not restored");
        check(configuration==(mode==17 || mode==24?0U:1U),"native configuration changed on close");
        if(mode==16)check(!state_reads,"configuration timeout treated as STALL and used native state");
        if(mode==23)check(state_reads && !alternate_queries,"redundant hanging GET_INTERFACE was sent despite known idle state");
        if(mode==26)check(state_reads && alternate_queries,"unknown native alternate did not use the wire query");
        FILE *report=tmpfile();check(report!=NULL,"temporary diagnostic report");
        usb_audio_hub_debug(report);usb_audio_topology_debug(report);rewind(report);
        unsigned verified,failed,restore_errors;
        check(fscanf(report,"usb_audio_hubs resumed_ports=%u failed_ports=%u restore_errors=%u",
                     &verified,&failed,&restore_errors)==3,"missing wake summary");
        char details[8192];rewind(report);size_t length=fread(details,1,sizeof(details)-1,report);
        details[length]=0;fclose(report);
        if(mode<=3 || mode==6 || mode==7) {
            check(!failed && verified==(mode==3?0U:hubs),"successful wake reported as failure");
        }
        if(mode==9)check(failed && strstr(details,"stage=initial port status request"),"status fault lost");
        if(mode==10)check(failed && strstr(details,"stage=clear suspend request"),"command fault lost");
        if(mode==11)check(strstr(details,"stage=device header") && strstr(details,"req=06 value=0100"),"descriptor transfer fault lost");
        if(mode==12)check(strstr(details,"stage=parse playback formats") && strstr(details,"formats=0"),"format rejection stage lost");
        if(mode==13)check(strstr(details,"stage=get alternate") && strstr(details,"req=0a"),"alternate request failure lost");
        if(mode==14 || mode==17 || mode==21 || mode==24)check(strstr(details,"config_cached=1"),"configuration fallback missing");
        if(mode==18 || mode==21 || mode==24)check(strstr(details,"alternate_cached=1"),"alternate fallback missing");
        check(clock_ms<5000,"resume wait was unbounded");
        check(usb_test_registers[4]==(RUN|ASE|PSE) && usb_test_registers[6]==0x27 &&
              usb_test_registers[9]==0x12000000 && usb_test_registers[10]==0x12001000,
              "native controller schedule not restored");
        printf("PASS normal hub audio case%u repeat%u: resumes=%u restores=%u\n",mode,repeat,resumes,suspends);
      }
    }
    return 0;
}
