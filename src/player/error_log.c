#include "player_data.h"

FILE *player_error_log_open(const char *path,bool append)
{
    /* Start a fresh report after 64 KiB. The record currently being written
     * stays complete; repeated automatic errors cannot grow the file forever. */
    if(append) {
        FILE *previous=fopen(path,"rb");
        if(previous) {
            if(!fseek(previous,0,SEEK_END) && ftell(previous)>=65536L)append=false;
            fclose(previous);
        }
    }
    return fopen(path,append?"ab":"wb");
}
