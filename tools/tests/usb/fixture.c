#include <stdbool.h>
bool ndvideo_usb_fs_poll(void)
{
    return true;
}

#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *image;
static const unsigned sectors = 131072;
DSTATUS disk_initialize(BYTE d)
{
    return d ? STA_NOINIT : 0;
}
DSTATUS disk_status(BYTE d)
{
    return disk_initialize(d);
}
DRESULT disk_read(BYTE d, BYTE *p, LBA_t s, UINT n)
{
    if (d || s >= sectors || n > sectors - s)
        return RES_PARERR;
    memcpy(p, image + (size_t)s * 512, n * 512);
    return RES_OK;
}
DRESULT disk_write(BYTE d, const BYTE *p, LBA_t s, UINT n)
{
    if (d || s >= sectors || n > sectors - s)
        return RES_PARERR;
    memcpy(image + (size_t)s * 512, p, n * 512);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE d, BYTE c, void *p)
{
    if (d)
        return RES_PARERR;
    switch (c) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)p = 512;
        break;
    case GET_SECTOR_COUNT:
        *(LBA_t *)p = sectors;
        break;
    case GET_BLOCK_SIZE:
        *(DWORD *)p = 1;
        break;
    default:
        return RES_PARERR;
    }
    return RES_OK;
}
DWORD get_fattime(void)
{
    return 0;
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    unsigned formats[] = {FM_FAT | FM_SFD, FM_FAT32 | FM_SFD, FM_EXFAT | FM_SFD, FM_FAT32};
    unsigned which = atoi(argv[1]);
    assert(which < 4);
    image = calloc(sectors, 512);
    assert(image);
    BYTE work[4096];
    MKFS_PARM opts = {.fmt = formats[which], .n_fat = 1, .au_size = which == 0 ? 4096 : 512};
    assert(f_mkfs("0:", &opts, work, sizeof(work)) == FR_OK);
    FATFS fs;
    assert(f_mount(&fs, "0:", 1) == FR_OK);
    assert(f_mkdir("0:/ndvideo") == FR_OK);
    assert(f_mkdir("0:/ndvideo/Rick and Morty") == FR_OK);
    assert(f_mkdir("0:/ndvideo/Rick and Morty/Season 1") == FR_OK);
    FIL file, fragment;
    UINT bytes;
    assert(f_open(&fragment, "0:/fragment.bin", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    assert(f_open(&file, "0:/ndvideo/Rick and Morty/Season 1/Episode 1.nvp.tns",
                  FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    for (unsigned j = 0; j < 128; ++j) {
        for (unsigned i = 0; i < sizeof(work); ++i)
            work[i] = (unsigned char)(((j * 4096 + i) * 73) ^ ((j * 4096 + i) >> 3) ^ 0xa5);
        assert(f_write(&file, work, sizeof(work), &bytes) == FR_OK && bytes == sizeof(work));
        assert(f_write(&fragment, work, sizeof(work), &bytes) == FR_OK && bytes == sizeof(work));
    }
    assert(f_close(&file) == FR_OK);
    assert(f_close(&fragment) == FR_OK);
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    FILE *out = fopen(argv[2], "wb");
    assert(out && fwrite(image, 512, sectors, out) == sectors);
    assert(!fclose(out));
    free(image);
    return 0;
}
