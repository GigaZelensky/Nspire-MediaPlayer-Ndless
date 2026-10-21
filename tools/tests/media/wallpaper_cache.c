#include "player/wallpaper_image.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "player/wallpaper_data.inc"
static unsigned decodes;
static unsigned services;
static void service(void *context){assert(context==&services);++services;}
int __real_uncompress(Bytef *,uLongf *,const Bytef *,uLong);
int __wrap_uncompress(Bytef *out,uLongf *bytes,const Bytef *in,uLong count)
{++decodes;return __real_uncompress(out,bytes,in,count);}
int main(void)
{
    uint16_t *original=malloc(153600);assert(original);uLongf bytes=153600;
    assert(__real_uncompress((Bytef *)original,&bytes,wallpaper_data,sizeof(wallpaper_data))==Z_OK);
    const uint16_t *a=wallpaper_image(0xfa80);assert(a && decodes==1);
    for(unsigned i=0;i<76800;++i)assert(a[i]==wallpaper_darken(wallpaper_saturate(original[i])));
    for(unsigned i=0;i<100;++i)assert(wallpaper_image(0xfa80)==a);
    assert(decodes==1);
    const uint16_t *b=wallpaper_image(0x2cbf);assert(b && b!=a && wallpaper_previous()==a && decodes==2);
    for(unsigned i=0;i<76800;++i)assert(b[i]==wallpaper_darken(wallpaper_recolor(wallpaper_saturate(original[i]),0x2cbf)));
    for(unsigned i=0;i<100;++i)assert(wallpaper_image(0x2cbf)==b);
    assert(decodes==2);
    wallpaper_finish_transition();assert(!wallpaper_previous() && wallpaper_image(0x2cbf)==b);
    wallpaper_release();assert(!wallpaper_previous());
    assert(wallpaper_image(0xfa80) && decodes==3);
    wallpaper_finish_transition();
    wallpaper_set_service(service,&services);
    const uint16_t *cooperative=wallpaper_image(0x2cbf);
    assert(cooperative && services>=120 && decodes==3);
    for(unsigned i=0;i<76800;++i)assert(cooperative[i]==wallpaper_darken(wallpaper_recolor(wallpaper_saturate(original[i]),0x2cbf)));
    unsigned before=services;
    assert(wallpaper_image(0x2cbf)==cooperative && services==before);
    wallpaper_set_service(NULL,NULL);wallpaper_service();assert(services==before);
    wallpaper_release();free(original);
    puts("PASS actual zlib decode, stable cached frames, sliced theme preparation/service, crossfade ownership and release/reopen");
}
