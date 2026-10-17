#include "audio/extra_audio.h"
#include "codecs/codec_module_api.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
extern int module_entry(const CodecHostApi *,CodecModuleApi *);
static unsigned shutdowns;
bool module_runtime_bind(const CodecHostApi *host)
{return host && host->abi_version==CODEC_MODULE_ABI_VERSION && host->struct_size==sizeof(*host);}
void module_runtime_shutdown(void) {++shutdowns;}
typedef struct {FILE *file;uint64_t bytes,position;} Source;
static size_t read_source(void *c,void *dst,size_t n)
{Source *s=c;size_t got=fread(dst,1,n,s->file);s->position+=got;return got;}
static bool seek_source(void *c,uint64_t p)
{Source *s=c;if(p>s->bytes || p>0x7fffffff || fseek(s->file,(long)p,SEEK_SET))return false;s->position=p;return true;}
static uint64_t tell_source(void *c){return ((Source *)c)->position;}
static void *resolve(const char *name){(void)name;return NULL;}
static AudioInput open_source(const char *path,Source *s)
{
    s->file=fopen(path,"rb");assert(s->file && !fseek(s->file,0,SEEK_END));
    long size=ftell(s->file);assert(size>0 && !fseek(s->file,0,SEEK_SET));
    s->bytes=(uint64_t)size;s->position=0;
    return (AudioInput){s,read_source,seek_source,tell_source,s->bytes};
}
int main(int argc,char **argv)
{
    assert(argc==2);
    CodecHostApi host={CODEC_MODULE_ABI_VERSION,sizeof(host),resolve,NULL};
    CodecModuleApi module={0};
    assert(module_entry(NULL,&module)!=0);
    assert(!module_entry(&host,&module));
    assert(module.codec_id==NDVIDEO_AUDIO_MODULE_CODEC && module.codec_api_size==sizeof(AudioModuleApi));
    const AudioModuleApi *api=module.codec_api;
    assert(api->audio_size==sizeof(ExtraAudio) && api->input_size==sizeof(AudioInput));
    for(unsigned repeat=0;repeat<3;++repeat) {
        Source first,second;
        ExtraAudio *a=api->open(open_source(argv[1],&first));assert(a && !module.shutdown());
        ExtraAudio *b=api->open(open_source(argv[1],&second));assert(b && !module.shutdown());
        int16_t pcm[1024*2];uint32_t total=0;
        for(;;) {int n=a->ops->read(a,pcm,1024);assert(n>=0 && n<=1024);if(!n)break;total+=(unsigned)n;}
        assert(total==a->frames && total>0);
        assert(a->ops->seek(a,total/2));assert(a->ops->read(a,pcm,514)>0);
        a->ops->close(a);free(a);assert(!module.shutdown());
        assert(b->ops->read(b,pcm,514)>0);b->ops->close(b);free(b);
        assert(module.shutdown());assert(!fclose(first.file) && !fclose(second.file));
    }
    assert(shutdowns==3);
    printf("PASS audio module %u: two live contexts, exact EOF, seek, unload refusal and repeated close/reopen\n",module.codec_id);
    return 0;
}
