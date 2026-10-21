#ifndef NMEDIA_BATTERY_NATIVE_H
#define NMEDIA_BATTERY_NATIVE_H
#include "portable_resolver.h"

enum {
    BATTERY_NATIVE_PERCENT=1U, BATTERY_NATIVE_CHARGE_MODE=2U,
    BATTERY_NATIVE_THRESHOLDS=4U, BATTERY_NATIVE_CACHED_VOLTAGE=8U,
    BATTERY_NATIVE_CALIBRATION=16U,
    /* Reserved; no inspected family establishes a safe known-full event. */
    BATTERY_NATIVE_KNOWN_FULL=32U
};
enum {
    BATTERY_NATIVE_BAD_VIEW=1U, BATTERY_NATIVE_AMBIGUOUS=2U,
    BATTERY_NATIVE_CHANGED=4U
};
typedef struct {
    uint32_t capabilities,errors,scan_next,scan_end,scanned_words,view_begin,view_end;
    uint32_t percent_entry,percent_state,percent_table,percent_index_offset;
    uint32_t percent_semaphore_offset,percent_event_offset,percent_take,percent_give;
    uint32_t charge_entry,charge_state,charge_take,charge_give;
    uint32_t adc_entry,adc_global,converter_entry;
    unsigned percent_matches,charge_matches,adc_matches,converter_matches;
    bool initialized,complete;
} BatteryNative;
typedef struct {
    uint32_t capabilities,errors,percent,level_index,charge_mode;
    uint32_t levels[7],thresholds[7][2];
    uint32_t voltage_mv,raw,scale,reference_mv,calibration_low,calibration_high;
    uint32_t mapped_channel,calibration_fingerprint;
    uint32_t batch_stamp,batch_duration,busy,count;
    /* A cached voltage is never claimed fresh by this module. The fingerprint
     * describes ADC trim, not the identity of a replaceable battery. */
} BatteryNativeSnapshot;

void battery_native_init(BatteryNative *);
/* Caller supplies a stable readable view with IRQ/FIQ excluded. Scans at most
 * min(word_budget,4096) words per call and at most the first4MiB of OS code.
 * Returns true once the bounded scan finishes. No discovered code is called. */
bool battery_native_resolve_step(BatteryNative *,const PortableView *,unsigned word_budget);
bool battery_native_snapshot(const BatteryNative *,const PortableView *,BatteryNativeSnapshot *);
/* Mapping-only adapter: refreshes PortableReaderPlatform's MMU/view proof,
 * never its storage capture, file APIs or export resolver. It reads battery
 * metadata only, so it takes CPU exclusion but no NAND lease. Call in normal
 * app foreground; it restores the exact incoming CPU mask. Initial discovery
 * accepts at most 16384 words, divided into bounded 4096-word slices.
 * Repeated foreground calls advance discovery; no startup-wide blocking scan. */
bool battery_native_default(BatteryNative *,BatteryNativeSnapshot *,unsigned word_budget);
#endif
