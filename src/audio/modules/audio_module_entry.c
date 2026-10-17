#if defined(NDVIDEO_BUILD_AUDIO_MODULE)
#include "audio/extra_audio.h"
#include "codecs/codec_module_api.h"
#include "codecs/modules/module_runtime.h"
#include <stdlib.h>

static unsigned live;
static int module_read(ExtraAudio *a,int16_t *pcm,unsigned frames)
{ return a->delegate_ops->read(a,pcm,frames); }
static bool module_seek(ExtraAudio *a,uint32_t frame)
{ return a->delegate_ops->seek(a,frame); }
static void module_close(ExtraAudio *a)
{ a->delegate_ops->close(a);if(live)--live; }
static const ExtraAudioOps operations={module_read,module_seek,module_close};
static ExtraAudio *module_open(AudioInput input)
{
#if NDVIDEO_AUDIO_MODULE_CODEC==5
    ExtraAudio *a=extra_aac_open(input);
#else
    ExtraAudio *a=extra_ogg_open(input);
#endif
    if (a) {a->delegate_ops=a->ops;a->ops=&operations;++live;}
    return a;
}
static bool module_shutdown(void)
{ if (live) return false;module_runtime_shutdown();return true; }
static const AudioModuleApi api={sizeof(api),sizeof(ExtraAudio),sizeof(AudioInput),
                                 NDVIDEO_AUDIO_MODULE_CODEC,module_open};
int module_entry(const CodecHostApi *host,CodecModuleApi *out)
{
    if (!out || !module_runtime_bind(host)) return -1;
    *out=(CodecModuleApi){CODEC_MODULE_ABI_VERSION,sizeof(*out),NDVIDEO_AUDIO_MODULE_CODEC,
                         sizeof(api),&api,module_shutdown};
    return 0;
}
#endif
