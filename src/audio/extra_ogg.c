#include "extra_audio.h"
#include "media_config.h"
#include "vendor/tremor/ivorbisfile.h"
#include <opusfile.h>
#include <limits.h>
#include <stdlib.h>
#if NDMEDIA_WITH_VORBIS && (!defined(NDVIDEO_AUDIO_MODULE_CODEC) || NDVIDEO_AUDIO_MODULE_CODEC==4)
#define WITH_TREMOR 1
#endif
#if NDMEDIA_WITH_OPUS && (!defined(NDVIDEO_AUDIO_MODULE_CODEC) || NDVIDEO_AUDIO_MODULE_CODEC==6)
#define WITH_OPUS 1
#endif

typedef struct {
    OggVorbis_File vorbis;
    OggOpusFile *opus;
    bool vorbis_open;
} OggState;
#if WITH_TREMOR
static size_t vorbis_read(void *dst,size_t size,size_t count,void *context)
{
    ExtraAudio *a=context;
    if (!size || count>SIZE_MAX/size) return 0;
    return a->input.read(a->input.context,dst,size*count)/size;
}
#endif
static int ogg_seek(void *context,ogg_int64_t offset,int whence)
{ return extra_input_seek(&((ExtraAudio *)context)->input,offset,whence)?0:-1; }
#if WITH_TREMOR
static long vorbis_tell(void *context)
{
    ExtraAudio *a=context;
    uint64_t position=a->input.tell(a->input.context);
    return position>LONG_MAX?-1:(long)position;
}
#endif
#if WITH_OPUS
static opus_int64 opus_tell(void *context)
{ ExtraAudio *a=context;return (opus_int64)a->input.tell(a->input.context); }
static int opus_read(void *context,unsigned char *data,int bytes)
{
    ExtraAudio *a=context;
    return bytes<0?-1:(int)a->input.read(a->input.context,data,(size_t)bytes);
}
static bool seek_opus(ExtraAudio *a,uint32_t frame)
{
    OggState *s=a->state;
    /* libopusfile already decodes its minimum 80 ms preroll. Real speech can
     * retain more filter-state error than our short fixtures after that seek.
     * An extra 80 ms of decoded/discarded PCM also passed the full-episode
     * comparison, without changing the requested PCM position or playback. */
    uint32_t at=frame>3840U?frame-3840U:0;
    if(op_pcm_seek(s->opus,at))return false;
    opus_int16 scratch[512];
    while(at<frame) {
        unsigned count=frame-at>256U?256U:frame-at;
        int read=op_read(s->opus,scratch,(int)(count*a->channels),NULL);
        if(read<=0)return false;
        at+=(unsigned)read;
    }
    return op_pcm_tell(s->opus)==frame;
}
#endif
static void close_ogg(ExtraAudio *a)
{
    OggState *s=a->state;
    if (!s) return;
#if WITH_OPUS
    if (s->opus) op_free(s->opus);
#endif
#if WITH_TREMOR
    if (s->vorbis_open) ov_clear(&s->vorbis);
#endif
    free(s);
}
static bool seek_ogg(ExtraAudio *a,uint32_t frame)
{
    OggState *s=a->state;
#if WITH_OPUS
    if (s->opus) return seek_opus(a,frame);
#endif
#if WITH_TREMOR
    return ov_pcm_seek(&s->vorbis,frame)==0 && ov_pcm_tell(&s->vorbis)==frame;
#else
    return false;
#endif
}
static int read_ogg(ExtraAudio *a,int16_t *pcm,unsigned frames)
{
    OggState *s=a->state;
    int link;
#if WITH_OPUS
    if (s->opus) return op_read(s->opus,pcm,(int)(frames*a->channels),&link);
#endif
#if WITH_TREMOR
    long bytes=ov_read(&s->vorbis,(char *)pcm,(int)(frames*a->channels*2),&link);
    return bytes<0?-1:(int)(bytes/(a->channels*2));
#else
    return -1;
#endif
}
static const ExtraAudioOps ops={read_ogg,seek_ogg,close_ogg};
ExtraAudio *extra_ogg_open(AudioInput input)
{
    ExtraAudio *a=calloc(1,sizeof(*a));
    OggState *s=calloc(1,sizeof(*s));
    if (!a || !s) {free(a);free(s);return NULL;}
    *a=(ExtraAudio){.input=input,.ops=&ops,.state=s};
    int64_t frames=-1;
#if WITH_OPUS
    OpusFileCallbacks opus_callbacks={opus_read,ogg_seek,opus_tell,NULL};
    int error;
    s->opus=op_open_callbacks(a,&opus_callbacks,NULL,0,&error);
    if (s->opus) {
        const OpusHead *head=op_head(s->opus,0);
        a->rate=48000;a->channels=(unsigned)head->channel_count;a->codec=6;
        frames=op_pcm_total(s->opus,-1);
        for (int i=0;i<op_link_count(s->opus);++i)
            if ((unsigned)op_head(s->opus,i)->channel_count!=a->channels) goto fail;
    } else
#endif
    {
#if WITH_TREMOR
        if (input.bytes>LONG_MAX || !input.seek(input.context,0)) goto fail;
        ov_callbacks callbacks={vorbis_read,ogg_seek,NULL,vorbis_tell};
        if (ov_open_callbacks(a,&s->vorbis,NULL,0,callbacks)) goto fail;
        s->vorbis_open=true;
        vorbis_info *info=ov_info(&s->vorbis,0);
        a->rate=(uint32_t)info->rate;a->channels=(unsigned)info->channels;a->codec=4;
        frames=ov_pcm_total(&s->vorbis,-1);
        for (long i=0;i<ov_streams(&s->vorbis);++i) {
            info=ov_info(&s->vorbis,(int)i);
            if ((unsigned)info->channels!=a->channels || (uint32_t)info->rate!=a->rate) goto fail;
        }
#else
        goto fail;
#endif
    }
    if (a->channels<1 || a->channels>2 || a->rate<8000 || a->rate>96000 ||
        frames<=0 || frames>UINT32_MAX) goto fail;
    a->frames=(uint32_t)frames;
    if (!seek_ogg(a,0)) goto fail;
    return a;
fail:
    close_ogg(a);free(a);return NULL;
}
