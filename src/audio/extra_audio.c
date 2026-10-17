#include "extra_audio.h"
#include "media_config.h"
#include <stdlib.h>
#include <string.h>
#if NDVIDEO_AUDIO_MODULES
#include "codecs/codec_module_api.h"
#endif

bool extra_input_seek(AudioInput *input, int64_t offset, int whence)
{
    if (whence<0 || whence>2) return false;
    uint64_t base=whence==0?0:whence==1?input->tell(input->context):input->bytes;
    uint64_t position;
    if (offset<0) {
        uint64_t amount=(uint64_t)(-(offset+1))+1;
        if (amount>base) return false;
        position=base-amount;
    } else {
        if ((uint64_t)offset>UINT64_MAX-base) return false;
        position=base+(uint64_t)offset;
    }
    return position<=input->bytes && input->seek(input->context,position);
}
#if !defined(NDVIDEO_BUILD_AUDIO_MODULE)
ExtraAudio *extra_audio_open(AudioInput input,const uint8_t signature[4])
{
#if NDVIDEO_AUDIO_MODULES
    unsigned id=CODEC_MODULE_AAC;
    if (!memcmp(signature,"OggS",4)) {
        uint8_t header[27],packet[8];
        if (!input.seek(input.context,0) || input.read(input.context,header,27)!=27 ||
            header[4] || (header[5]&1) || !header[26] ||
            !input.seek(input.context,27U+header[26]) || input.read(input.context,packet,8)!=8)
            return NULL;
        if (!memcmp(packet,"OpusHead",8)) id=CODEC_MODULE_OPUS;
        else if (!memcmp(packet,"\1vorbis",7)) id=CODEC_MODULE_VORBIS;
        else return NULL;
    }
    if(!media_audio_codec_supported(id))return NULL;
    if (!input.seek(input.context,0)) return NULL;
    const CodecModuleApi *module=codec_module_acquire(id);
    if (!module) return NULL;
    const AudioModuleApi *api=module->codec_api;
    ExtraAudio *audio=NULL;
    if (module->codec_api_size==sizeof(*api) && api && api->struct_size==sizeof(*api) &&
        api->audio_size==sizeof(ExtraAudio) && api->input_size==sizeof(AudioInput) && api->codec==id && api->open)
        audio=api->open(input);
    if (!audio) {codec_module_release(id);codec_modules_trim_idle();return NULL;}
    audio->module_slot=id+1;
    return audio;
#else
    if(!memcmp(signature,"OggS",4)) {
#if NDMEDIA_WITH_VORBIS || NDMEDIA_WITH_OPUS
        return extra_ogg_open(input);
#else
        return NULL;
#endif
    }
#if NDMEDIA_WITH_AAC
    return extra_aac_open(input);
#else
    return NULL;
#endif
#endif
}
#endif
void extra_audio_close(ExtraAudio *a)
{
    if (!a) return;
    unsigned slot=a->module_slot;
    a->ops->close(a);free(a);
#if NDVIDEO_AUDIO_MODULES
    if (slot) {codec_module_release(slot-1);codec_modules_trim_idle();}
#else
    (void)slot;
#endif
}
