#include "hardware_acceleration.h"
/* Explicit test variants patch this initialized word in the packaged host.
 * Volatile prevents compile-time removal of disabled paths: layout and code
 * remain identical for controlled comparisons. Production enables quad NAND,
 * larger USB reads and hardware SHA/HMAC. */
__attribute__((used,externally_visible)) volatile uint32_t hardware_acceleration_mask=
    HW_ACCEL_QUAD | HW_ACCEL_USB_LARGE | HW_ACCEL_SHA;
