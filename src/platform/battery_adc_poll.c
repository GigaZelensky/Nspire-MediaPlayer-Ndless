#include "battery_adc_poll.h"
#if NDVIDEO_BATTERY_ACQUISITION || NDMEDIA_BATTERY_SERVICE
#include <string.h>
enum {ADC=0x900b0000U,GATE=0x90140060U,RTC=0x90090000U,TIMER=0x900d0024U,GATE_BIT=1U<<20};
static uint32_t rd(AdcPollBus *b,uint32_t at){return b->read(b->context,at);}
static void wr(AdcPollBus *b,uint32_t at,uint32_t value){b->write(b->context,at,value);b->barrier(b->context);}
static void acknowledge(AdcPollBus *b)
{
    /* Native10003b50/start and10003bfc/stop preserve all observed status
     * bits in each W1C acknowledgement. Real hardware also sets ff00 after
     * the conversion bank completes; an absolute3 write leaves those set. */
    wr(b,ADC+0x10c,rd(b,ADC+0x10c)|1U);
    wr(b,ADC+0x10c,rd(b,ADC+0x10c)|2U);
}
typedef struct {uint32_t previous;unsigned stagnant;} PollDeadline;
static bool expired(AdcPollBus *b,AdcPollResult *s,PollDeadline *d)
{
    uint32_t current=rd(b,TIMER);
    if(current==d->previous)++d->stagnant;else d->stagnant=0;
    d->previous=current;
    if(++s->polls>=1048576U)s->timeout_reason=1;
    else if(((s->start_ticks-current)&65535U)>=256U)s->timeout_reason=3;
    else if(d->stagnant>=4096U)s->timeout_reason=4;
    else if((uint32_t)(rd(b,RTC)-s->start_rtc)>=2U)s->timeout_reason=2;
    return s->timeout_reason!=0;
}
void adc_poll(AdcPollBus b,unsigned channel,unsigned reference,AdcPollResult *s)
{
    if(!s)return;
    memset(s,0,sizeof(*s));s->result=ADC_POLL_ARGUMENT;
    if(!b.read || !b.write || !b.barrier || channel>=8 || reference>1)return;
    s->start_rtc=rd(&b,RTC);s->start_ticks=rd(&b,TIMER);s->gate_before=rd(&b,GATE);
    s->timer_control_before=rd(&b,0x900d0028U);s->timer_source_before=rd(&b,0x900d0080U);
    /* The settling and foreground deadline are validated only for this
     * running 32768Hz source and unprescaled 16/32-bit downcounter. */
    if((s->timer_control_before&~2U)!=0x80U || s->timer_source_before!=0x0aU) {
        s->result=ADC_POLL_CLOCK;s->restored=true;
        s->timer_control_after=s->timer_control_before;s->timer_source_after=s->timer_source_before;
        s->gate_after=s->gate_before;s->end_rtc=s->start_rtc;s->end_ticks=s->start_ticks;return;
    }
    PollDeadline deadline={s->start_ticks,0};
    wr(&b,GATE,s->gate_before|GATE_BIT);
    if(!(rd(&b,GATE)&GATE_BIT)){s->result=ADC_POLL_GATE;goto gate_restore;}
    s->control_before=rd(&b,ADC+0x100);s->reference_before=rd(&b,ADC+0x104);
    s->interrupt_before=rd(&b,ADC+0x108);s->status_before=rd(&b,ADC+0x10c);
    /* Never borrow an active or completed-but-unconsumed native conversion.
     * Hardware can retain bit1 alone at idle (observed with native busy/count
     * zero). The native starter clears that flag before every conversion.
     * Preserve a pending completion bit0 and reject unknown status bits. */
    if(s->control_before!=0x71100U || (s->status_before&~2U) ||
       s->reference_before>1 || (s->interrupt_before&~3U)) {
        s->control_after=rd(&b,ADC+0x100);s->reference_after=rd(&b,ADC+0x104);
        s->interrupt_after=rd(&b,ADC+0x108);s->status_after=rd(&b,ADC+0x10c);
        s->result=ADC_POLL_BUSY;goto gate_restore;
    }
    s->touched=true;
    /* Suppress peripheral IRQ generation during polling. A completed flag is
     * acknowledged before restoring its original enable word, so no sample
     * from this private batch can invoke the native ADCdone HISR. */
    wr(&b,ADC+0x108,s->interrupt_before&~3U);
    wr(&b,ADC+0x104,reference);
    for(s->attempts=0;s->attempts<20 && s->conversions<10;++s->attempts) {
        acknowledge(&b);
        wr(&b,ADC+0x100,0x70111U);
        if(!s->attempts)s->first_status=rd(&b,ADC+0x10c);
        /* Native10003b9c waits for five ticks of this same 16-bit downcounter
         * after each start. Preserve that settling interval without invoking
         * the native PMU sleep request or changing any timer configuration. */
        uint32_t settle=rd(&b,TIMER);
        while(((settle-rd(&b,TIMER))&65535U)<5U)
            if(expired(&b,s,&deadline)) {
                s->result=ADC_POLL_TIMEOUT;goto restore;
            }
        uint32_t status;
        for(;;) {
            if(expired(&b,s,&deadline)) {s->result=ADC_POLL_TIMEOUT;goto restore;}
            status=rd(&b,ADC+0x10c);
            s->last_status=status;
            if(status&3U)break;
        }
        if(status&2U){++s->error_retries;continue;}
        uint32_t raw=rd(&b,ADC+channel*4U);
        if(raw>4095U){s->result=ADC_POLL_DATA;goto restore;}
        s->samples[s->conversions++]=raw;
    }
    if(s->conversions!=10){s->result=ADC_POLL_DATA;goto restore;}
    uint32_t sum=0,low=UINT32_MAX,high=0;
    for(unsigned i=0;i<10;++i) {
        uint32_t v=s->samples[i];sum+=v;if(v<low)low=v;if(v>high)high=v;
    }
    s->raw=(sum-low-high)/8U;s->result=ADC_POLL_OK;
restore:
    /* Native completion acknowledges status before writing the stop word.
     * Its isolated idle flag may remain/reappear; only a pending completion
     * or unknown bits prevent a verified return to the authenticated idle. */
    acknowledge(&b);
    wr(&b,ADC+0x100,s->control_before);
    wr(&b,ADC+0x104,s->reference_before);
    wr(&b,ADC+0x108,s->interrupt_before);
    s->control_after=rd(&b,ADC+0x100);s->reference_after=rd(&b,ADC+0x104);
    s->interrupt_after=rd(&b,ADC+0x108);s->status_after=rd(&b,ADC+0x10c);
gate_restore:
    wr(&b,GATE,(rd(&b,GATE)&~GATE_BIT)|(s->gate_before&GATE_BIT));
    s->gate_after=rd(&b,GATE);s->end_rtc=rd(&b,RTC);s->end_ticks=rd(&b,TIMER);
    s->timer_control_after=rd(&b,0x900d0028U);s->timer_source_after=rd(&b,0x900d0080U);
    s->restored=((s->gate_after^s->gate_before)&GATE_BIT)==0 && (!s->touched ||
        (s->control_after==s->control_before && s->reference_after==s->reference_before &&
         s->interrupt_after==s->interrupt_before && (s->status_after&~2U)==0));
    s->restored=s->restored && s->timer_control_before==s->timer_control_after &&
        s->timer_source_before==s->timer_source_after;
    if(!s->restored)s->result=ADC_POLL_RESTORE;
}

#endif
