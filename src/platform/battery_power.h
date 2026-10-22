#ifndef NMEDIA_BATTERY_POWER_H
#define NMEDIA_BATTERY_POWER_H
#include "../storage/usb/usb_registers.h"

/* Read-only PHY status. A/B identity must qualify session-valid: an OTG
 * host drives its own VBUS and must never be reported as external power.
 * FOTG210 OTGCSR: ID21, device-role20, B-session-valid17, B-session-end16,
 * local A-bus request4 (Faraday U-Boot
 * include/usb/fotg210.h). Original CX uses TDI OTGSC: ID8, BSV11, BSE12. */
static inline bool battery_power_decode(uint32_t asic,uint32_t cap,uint32_t identity,
                                       uint32_t otg,bool *external,bool *host)
{
    UsbRegisters r;
    if(!external || !host || !usb_registers_for_asic(asic,&r) || !otg ||
       !usb_registers_id_readable(&r,cap,identity,otg))return false;
    uint32_t valid=1U<<(r.classic?11:17),end=1U<<(r.classic?12:16);
    if(!(otg&r.role_mask)){*external=false;*host=true;return true;}
    /* ID can release before the old A-device has stopped sourcing VBUS.
     * Its own voltage must not become a charging indication during handoff. */
    if(!r.classic && (!(otg&(1U<<20)) || (otg&(1U<<4)))) {
        *external=*host=false;return true;
    }
    if((otg&valid) && (otg&end))return false;
    if(!cap && !(otg&(valid|end)))return false;
    *external=(otg&valid)!=0;*host=false;return true;
}
/* Foreground only. Temporarily ungates the status window and restores it;
 * never resets the USB port, changes roles or touches charger pins. */
bool battery_usb_power(bool *external,bool *host);
#endif
