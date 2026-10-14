#ifndef NDVIDEO_USB_SCSI_COMMANDS_H
#define NDVIDEO_USB_SCSI_COMMANDS_H
#include <stdint.h>
#include <string.h>

/* Keep READ(10) on ordinary drives; use READ(16) for wide block addresses
 * or a transfer that crosses the 32-bit LBA boundary. */
static inline unsigned usb_scsi_read_command(uint8_t command[16], uint64_t sector, uint32_t count)
{
    if (!count || count-1U > UINT64_MAX-sector) return 0;
    memset(command,0,16);
    if (sector <= UINT32_MAX && count <= 65535U && count-1U <= UINT32_MAX-sector) {
        command[0]=0x28;
        for(unsigned i=0;i<4;++i)command[2+i]=(uint8_t)(sector>>(24-i*8));
        command[7]=(uint8_t)(count>>8);command[8]=(uint8_t)count;
        return 10;
    }
    command[0]=0x88;
    for(unsigned i=0;i<8;++i)command[2+i]=(uint8_t)(sector>>(56-i*8));
    for(unsigned i=0;i<4;++i)command[10+i]=(uint8_t)(count>>(24-i*8));
    return 16;
}
#endif
