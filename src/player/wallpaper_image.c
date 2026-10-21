#include "wallpaper_image.h"
#include <stdlib.h>
#include <zlib.h>
#include "wallpaper_data.inc"
static uint16_t *pixels,*previous;
static uint16_t loaded_accent;
static int attempted;
int wallpaper_cached(void){return pixels!=NULL;}
static void (*service_callback)(void *);
static void *service_context;
void wallpaper_set_service(void (*service)(void *),void *context)
{service_callback=service;service_context=context;}
void wallpaper_service(void)
{if(service_callback)service_callback(service_context);}
const uint16_t *wallpaper_image(uint16_t accent)
{
    if(attempted && loaded_accent==accent)return pixels;
    free(previous);previous=pixels;pixels=NULL;attempted=1;loaded_accent=accent;
    pixels=malloc(320U*240U*sizeof(*pixels));
    if(!pixels){free(previous);previous=NULL;return NULL;}
    uLongf bytes=320U*240U*sizeof(*pixels);
    int result;
    if(service_callback) {
        z_stream stream={0};
        stream.next_in=(Bytef *)wallpaper_data;stream.avail_in=sizeof(wallpaper_data);
        result=inflateInit(&stream);
        if(result==Z_OK) {
            unsigned done=0;
            do {
                unsigned chunk=(unsigned)bytes-done;
                if(chunk>2560)chunk=2560; /* Four rows, then service USB/decoder. */
                stream.next_out=(Bytef *)pixels+done;stream.avail_out=chunk;
                result=inflate(&stream,Z_NO_FLUSH);
                done=(unsigned)stream.total_out;
                wallpaper_service();
            } while(result==Z_OK && done<bytes);
            bytes=stream.total_out;
            inflateEnd(&stream);
            if(result==Z_STREAM_END)result=Z_OK;
            else if(result==Z_OK)result=Z_DATA_ERROR;
        }
    } else result=uncompress((Bytef *)pixels,&bytes,wallpaper_data,sizeof(wallpaper_data));
    if(result!=Z_OK || bytes!=320U*240U*sizeof(*pixels)) {
        free(pixels);free(previous);pixels=previous=NULL;return NULL;
    }
    for(unsigned i=0;i<320U*240U;++i) {
        pixels[i]=wallpaper_darken(wallpaper_recolor(wallpaper_saturate(pixels[i]),accent));
        if((i+1)%1280==0)wallpaper_service();
    }
    return pixels;
}
void wallpaper_release(void)
{free(pixels);free(previous);pixels=previous=NULL;attempted=0;}
const uint16_t *wallpaper_previous(void){return previous;}
void wallpaper_finish_transition(void){free(previous);previous=NULL;}
