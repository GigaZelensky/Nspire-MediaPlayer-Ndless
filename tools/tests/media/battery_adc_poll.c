#include "battery_adc_poll.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if NDVIDEO_BATTERY_ACQUISITION
typedef struct {
    uint32_t gate,control,reference,interrupt,status,rtc,timer,timer_control,timer_source,data[10];
    unsigned conversions,waits,reads,writes,barriers,start_writes;
    bool timeout,stopped_rtc,stopped_timer,gate_refused,bad_data,error_status,idle_flag,channel_flags;
} Fixture;
static uint32_t read_reg(void *context,uint32_t at)
{
    Fixture *f=context;++f->reads;
    switch(at) {
    case 0x90140060U:return f->gate;
    case 0x90090000U:if(f->timeout && !f->stopped_rtc && f->reads>100)f->rtc=2;return f->rtc;
    case 0x900d0024U:if(!f->stopped_timer)--f->timer;return f->timer;
    case 0x900d0028U:return f->timer_control;
    case 0x900d0080U:return f->timer_source;
    case 0x900b0100U:return f->control;
    case 0x900b0104U:return f->reference;
    case 0x900b0108U:return f->interrupt;
    case 0x900b010cU:
        if(f->control==0x71100U && f->idle_flag)f->status|=2;
        if(f->control==0x70111U && !f->timeout && ++f->waits==3)
            f->status=(f->error_status?2:1)|(f->channel_flags?0xff00U:0);
        return f->status;
    case 0x900b0004U:
        assert((f->status&3U)==1 && !f->interrupt);
        return f->bad_data?0xffff:f->data[f->conversions++];
    default:assert(!"Unexpected ADC register read");return 0;
    }
}
static void write_reg(void *context,uint32_t at,uint32_t value)
{
    Fixture *f=context;++f->writes;
    switch(at) {
    case 0x90140060U:if(!f->gate_refused)f->gate=value;return;
    case 0x900b0100U:
        assert(value==0x70111U || value==0x71100U);f->control=value;
        if(value==0x70111U){assert(!f->interrupt);++f->start_writes;f->waits=0;}return;
    case 0x900b0104U:assert(value<=1);f->reference=value;return;
    case 0x900b0108U:assert(value<=3);f->interrupt=value;return;
    case 0x900b010cU:assert(!(value&~0xff03U) && (value&3U));f->status&=~value;return;
    default:assert(!"Unexpected ADC register write");
    }
}
static void barrier(void *context){++((Fixture *)context)->barriers;}
static Fixture fresh(void)
{
    Fixture f={.gate=0xffe3ffffU,.control=0x71100U,.reference=1,.interrupt=3,
        .timer_control=0x80,.timer_source=0x0a};
    for(unsigned i=0;i<10;++i)f.data[i]=900+i;
    return f;
}
static AdcPollResult run(Fixture *f)
{AdcPollResult s;adc_poll((AdcPollBus){f,read_reg,write_reg,barrier},1,1,&s);assert(f->writes==f->barriers);return s;}
static void restored(Fixture *f,AdcPollResult *s)
{assert(s->restored && f->control==0x71100U && f->reference==1 && f->interrupt==3 && !(f->status&~2U) && !(f->gate&(1U<<20)));}

#endif
int main(void)
{
#if !NDVIDEO_BATTERY_ACQUISITION
    AdcPollResult out;adc_poll((AdcPollBus){0},1,1,&out);assert(out.result==ADC_POLL_ARGUMENT);
    puts("PASS production ADC acquisition compiled out");
#else
    Fixture f=fresh();AdcPollResult s=run(&f);
    assert(s.result==ADC_POLL_OK && s.raw==904 && s.conversions==10 && s.polls>=20 && f.start_writes==10);restored(&f,&s);
    f=fresh();f.gate|=1U<<20;s=run(&f);assert(s.result==ADC_POLL_OK && s.restored && (f.gate&(1U<<20)));
    f=fresh();f.control=0x70111U;s=run(&f);assert(s.result==ADC_POLL_BUSY && !s.touched && !f.start_writes && s.restored);
    f=fresh();f.status=1;s=run(&f);assert(s.result==ADC_POLL_BUSY && !s.touched && f.status==1 && s.restored);
    f=fresh();f.status=2;s=run(&f);assert(s.result==ADC_POLL_OK && s.status_before==2 && !s.status_after);restored(&f,&s);
    f=fresh();f.idle_flag=true;s=run(&f);assert(s.result==ADC_POLL_OK && s.status_before==2 && s.status_after==2);restored(&f,&s);
    /* Reproduce realhardware ff01 completion/ff02 idle flags. Native RMW
     * acknowledgements must clear these too; a fixed3 mask leaks them. */
    f=fresh();f.idle_flag=f.channel_flags=true;s=run(&f);
    assert(s.result==ADC_POLL_OK && s.conversions==10 && s.last_status==0xff01U && s.status_after==2);restored(&f,&s);
    f=fresh();f.status=3;s=run(&f);assert(s.result==ADC_POLL_BUSY && !s.touched && f.status==3 && s.status_after==3);
    f=fresh();f.status=4;s=run(&f);assert(s.result==ADC_POLL_BUSY && !s.touched && f.status==4);
    f=fresh();f.gate_refused=true;s=run(&f);assert(s.result==ADC_POLL_GATE && !s.touched && s.restored);
    f=fresh();f.timeout=true;s=run(&f);assert(s.result==ADC_POLL_TIMEOUT && s.polls<100 && s.start_rtc==0 && s.end_rtc==2);restored(&f,&s);
    f=fresh();f.timeout=f.stopped_rtc=true;s=run(&f);assert(s.result==ADC_POLL_TIMEOUT && s.timeout_reason==3 && s.polls<256);restored(&f,&s);
    f=fresh();f.stopped_timer=true;s=run(&f);assert(s.result==ADC_POLL_TIMEOUT && s.timeout_reason==4 && s.polls==4096);restored(&f,&s);
    f=fresh();f.timer_source=0;s=run(&f);assert(s.result==ADC_POLL_CLOCK && !f.writes && s.restored);
    f=fresh();f.timer_control=0;s=run(&f);assert(s.result==ADC_POLL_CLOCK && !f.writes && s.restored);
    f=fresh();f.bad_data=true;s=run(&f);assert(s.result==ADC_POLL_DATA);restored(&f,&s);
    f=fresh();f.error_status=true;s=run(&f);assert(s.result==ADC_POLL_DATA && s.attempts==20 && !s.conversions);restored(&f,&s);
    f=fresh();adc_poll((AdcPollBus){&f,read_reg,write_reg,barrier},8,1,&s);
    assert(s.result==ADC_POLL_ARGUMENT && !f.reads && !f.writes);
    puts("PASS polled ADC model: trimmed10-sample mean, masked peripheral IRQ, idle-only ownership, bounded error/dead/stopped-clock paths and register restoration");
#endif
    return 0;
}
