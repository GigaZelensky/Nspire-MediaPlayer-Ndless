#include "storage/usb/usb_scsi_commands.h"
#include <assert.h>
#include <stdio.h>
int main(void){uint8_t c[16];
 assert(usb_scsi_read_command(c,1234,32)==10&&c[0]==0x28&&c[4]==4&&c[5]==210&&c[8]==32);
 assert(usb_scsi_read_command(c,UINT32_MAX,1)==10);
 assert(usb_scsi_read_command(c,UINT32_MAX,2)==16&&c[0]==0x88&&c[5]==0&&c[6]==255&&c[9]==255&&c[13]==2);
 assert(usb_scsi_read_command(c,1ULL<<32,32)==16&&c[5]==1&&c[9]==0&&c[13]==32);
 assert(!usb_scsi_read_command(c,UINT64_MAX,2));assert(!usb_scsi_read_command(c,0,0));
 puts("PASS READ(10)/READ(16) selection, 64-bit LBA encoding and boundary/overflow checks");}
