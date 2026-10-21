#ifndef NMEDIA_BATTERY_ADC_POLL_H
#define NMEDIA_BATTERY_ADC_POLL_H
#include "../media_config.h"
#include <stdint.h>
#include <stdbool.h>
#ifndef NDVIDEO_BATTERY_ACQUISITION
#define NDVIDEO_BATTERY_ACQUISITION 0
#endif
typedef struct {
    void *context;
    uint32_t (*read)(void *,uint32_t);
    void (*write)(void *,uint32_t,uint32_t);
    void (*barrier)(void *);
} AdcPollBus;
enum {ADC_POLL_OK,ADC_POLL_ARGUMENT,ADC_POLL_GATE,ADC_POLL_BUSY,ADC_POLL_TIMEOUT,
      ADC_POLL_DATA,ADC_POLL_RESTORE,ADC_POLL_CLOCK};
typedef struct {
    unsigned result,conversions,attempts,polls;
    uint32_t samples[10],raw,start_rtc,end_rtc,start_ticks,end_ticks;
    uint32_t first_status,last_status,error_retries,timeout_reason;
    uint32_t timer_control_before,timer_control_after,timer_source_before,timer_source_after;
    uint32_t gate_before,gate_after,control_before,control_after;
    uint32_t reference_before,reference_after,interrupt_before,interrupt_after,status_before,status_after;
    bool touched,restored;
} AdcPollResult;
/* Caller has authenticated the exact native driver, idle state and channel,
 * and holds IRQ/FIQ exclusion throughout. No native calls or cache writes.
 * Native source:10003b20(start),10003f84(status),10003bec(stop),PMU gate table.
 * All register addresses are the firmware's established virtual addresses. */
#if NDVIDEO_BATTERY_ACQUISITION || NDMEDIA_BATTERY_SERVICE
void adc_poll(AdcPollBus bus,unsigned channel,unsigned reference,AdcPollResult *out);
#else
static inline void adc_poll(AdcPollBus bus,unsigned channel,unsigned reference,AdcPollResult *out)
{ (void)bus;(void)channel;(void)reference;if(out)*out=(AdcPollResult){.result=ADC_POLL_ARGUMENT}; }
#endif
#endif
