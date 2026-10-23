#include "audio_decoder.h"
#include <stdlib.h>
#include <string.h>
#define DR_WAV_NO_STDIO
#include "vendor/dr_wav.h"
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
#include "vendor/dr_flac.h"
#include "mp3_backend.h"
#include "extra_audio.h"
#if NDVIDEO_AUDIO_MODULES
#include "codecs/codec_module_api.h"
#endif
struct AudioDecoder {
    AudioInput input;
    AudioBuffer *audio;
    AudioDecodeStats *stats;
    uint32_t (*now)(void);
    drwav wav;
    drflac *flac;
    mp3dec_ex_t mp3;
    mp3dec_io_t mp3_io;
    ExtraAudio *extra;
    union {
        int16_t s16[1024 * 2];
        drwav_int32 s32[1024 * 2];
    } block;
    uint64_t read_bytes;
    uint32_t steps, max_step_ms;
    uint32_t logical_frames;
    bool wav_open, mp3_open, eof, io_failed;
    const char *error;
};
static char last_stream_report[256] = "not requested";
static void describe(const AudioDecoder *d, char *text, size_t capacity)
{
    snprintf(text, capacity,
             "read_bytes=%llu steps=%lu max_step_ms=%lu buffer_frames=%lu begin=%lu end=%lu eof=%u "
             "io_failed=%u pcm_missed=%u miss_index=%lu codec=%u backend=%s rate=%lu channels=%u",
             (unsigned long long)d->read_bytes, (unsigned long)d->steps,
             (unsigned long)d->max_step_ms, (unsigned long)d->audio->pcm_capacity,
             (unsigned long)d->audio->pcm_begin, (unsigned long)d->audio->pcm_end, d->eof,
             d->io_failed, d->audio->pcm_missed, (unsigned long)d->audio->pcm_miss_index,
             d->stats->codec, d->mp3_open ? "libmad" : d->extra ?
                d->stats->codec==4?"Tremor":d->stats->codec==5?"FAAD2-fixed":"Opus-fixed" :
                d->flac ? "dr_flac" : "dr_wav",
             (unsigned long)d->audio->sample_rate, d->audio->channels);
}
static size_t read_input(void *context, void *buffer, size_t bytes)
{
    AudioDecoder *d = context;
    uint32_t start = d->now();
    uint64_t position = d->input.tell(d->input.context);
    size_t got = d->input.read(d->input.context, buffer, bytes);
    if (got > bytes || position > d->input.bytes || got > d->input.bytes - position) {
        d->io_failed = true;
        got = 0;
    } else if (got < bytes && position + got < d->input.bytes)
        d->io_failed = true;
    d->read_bytes += got;
    d->stats->read_ms += d->now() - start;
    return got;
}
static bool seek_absolute(AudioDecoder *d, uint64_t position)
{
    if (position > d->input.bytes)
        return false;
    uint32_t start = d->now();
    bool ok = d->input.seek(d->input.context, position);
    d->stats->read_ms += d->now() - start;
    if (!ok)
        d->io_failed = true;
    return ok;
}
static bool seek_input(AudioDecoder *d, int offset, unsigned origin)
{
    uint64_t base = origin == 0   ? 0
                    : origin == 1 ? d->input.tell(d->input.context)
                                  : d->input.bytes;
    if (offset < 0) {
        uint64_t amount = (uint64_t)(-(int64_t)offset);
        if (amount > base)
            return false;
        return seek_absolute(d, base - amount);
    }
    if ((uint64_t)offset > UINT64_MAX - base)
        return false;
    return seek_absolute(d, base + (unsigned)offset);
}
static drwav_bool32 wav_seek(void *c, int n, drwav_seek_origin o)
{
    return seek_input(c, n, (unsigned)o);
}
static drflac_bool32 flac_seek(void *c, int n, drflac_seek_origin o)
{
    return seek_input(c, n, (unsigned)o);
}
static drwav_bool32 wav_tell(void *c, drwav_int64 *position)
{
    AudioDecoder *d = c;
    *position = (drwav_int64)d->input.tell(d->input.context);
    return DRWAV_TRUE;
}
static drflac_bool32 flac_tell(void *c, drflac_int64 *position)
{
    AudioDecoder *d = c;
    *position = (drflac_int64)d->input.tell(d->input.context);
    return DRFLAC_TRUE;
}
static size_t mp3_read(void *data, size_t bytes, void *c)
{
    return read_input(c, data, bytes);
}
static int mp3_seek(uint64_t position, void *c)
{
    return seek_absolute(c, position) ? 0 : -1;
}
static bool extra_seek(void *context,uint64_t position)
{ return seek_absolute(context,position); }
static uint64_t extra_tell(void *context)
{ AudioDecoder *d=context;return d->input.tell(d->input.context); }
void audio_decoder_close(AudioDecoder *d)
{
    if (!d)
        return;
    describe(d, last_stream_report, sizeof(last_stream_report));
    if (NDMEDIA_WITH_WAV && d->wav_open)
        drwav_uninit(&d->wav);
    if (NDMEDIA_WITH_FLAC && d->flac)
        drflac_close(d->flac);
    if (NDMEDIA_WITH_MP3 && d->mp3_open)
        mp3dec_ex_close(&d->mp3);
    extra_audio_close(d->extra);
    free(d);
}
AudioDecoder *audio_decoder_open(AudioBuffer *s, AudioInput input, AudioDecodeStats *stats,
                                 uint32_t (*now)(void), unsigned capacity, char *error,
                                 size_t error_capacity)
{
    memset(s, 0, sizeof(*s));
    memset(stats, 0, sizeof(*stats));
    if (!input.read || !input.seek || !input.tell || input.bytes < 12 || !now) {
        snprintf(error, error_capacity, "Invalid audio input");
        return NULL;
    }
    AudioDecoder *d = calloc(1, sizeof(*d));
    if (!d) {
        snprintf(error, error_capacity, "Decoder allocation failed");
        return NULL;
    }
    d->input = input;
    d->audio = s;
    d->stats = stats;
    d->now = now;
    d->error = "Could not open streamed audio";
    strcpy(last_stream_report, "opening");
    stats->encoded_bytes = input.bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)input.bytes;
    uint8_t signature[4];
    unsigned rate = 0, channels = 0, source_bits = 16;
    uint64_t frames = 0;
    if (!seek_absolute(d, 0) || read_input(d, signature, 4) != 4 || !seek_absolute(d, 0))
        goto failed;
    if (!memcmp(signature, "RIFF", 4) || !memcmp(signature,"RF64",4) || !memcmp(signature,"RIFX",4)) {
#if NDMEDIA_WITH_WAV
        if (!drwav_init_ex(&d->wav, read_input, wav_seek, wav_tell, NULL, d, NULL, 0, NULL))
            goto failed;
        d->wav_open = true;
        rate = d->wav.sampleRate;
        channels = d->wav.channels;
        frames = d->wav.totalPCMFrameCount;
        stats->codec = d->wav.translatedFormatTag == 17 ? 1 : 0;
        if (d->wav.translatedFormatTag == DR_WAVE_FORMAT_PCM ||
            d->wav.translatedFormatTag == DR_WAVE_FORMAT_IEEE_FLOAT)
            source_bits = d->wav.bitsPerSample;
#else
        d->error="WAV audio is not included in this build";goto failed;
#endif
    } else if (!memcmp(signature, "fLaC", 4)) {
#if NDMEDIA_WITH_FLAC
        d->flac = drflac_open(read_input, flac_seek, flac_tell, d, NULL);
        if (!d->flac)
            goto failed;
        rate = d->flac->sampleRate;
        channels = d->flac->channels;
        frames = d->flac->totalPCMFrameCount;
        stats->codec = 3;
        source_bits = d->flac->bitsPerSample;
#else
        d->error="FLAC is not included in this build";goto failed;
#endif
    } else {
        uint8_t prefix[12];
        if (read_input(d,prefix,sizeof(prefix))!=sizeof(prefix) || !seek_absolute(d,0)) goto failed;
        bool extended=!memcmp(prefix,"OggS",4) || !memcmp(prefix+4,"ftyp",4) ||
            !memcmp(prefix+4,"moov",4) || !memcmp(prefix+4,"free",4) ||
            (prefix[0]==255 && (prefix[1]&0xf6)==0xf0);
        if (!memcmp(prefix,"ID3",3) && !((prefix[6]|prefix[7]|prefix[8]|prefix[9])&128)) {
            uint64_t offset=10+((uint32_t)prefix[6]<<21)+((uint32_t)prefix[7]<<14)+
                ((uint32_t)prefix[8]<<7)+prefix[9]+((prefix[5]&16)?10U:0U);
            uint8_t tag_end[2];
            if (offset<=input.bytes && input.bytes-offset>=2 && seek_absolute(d,offset) &&
                read_input(d,tag_end,2)==2) extended=tag_end[0]==255 && (tag_end[1]&0xf6)==0xf0;
            if (!seek_absolute(d,0)) goto failed;
        }
        if (extended) {
#if NDMEDIA_WITH_EXTRA
            stats->codec=!memcmp(prefix,"OggS",4)?4:5;
            AudioInput measured={d,read_input,extra_seek,extra_tell,input.bytes};
            d->extra=extra_audio_open(measured,prefix);
            if (!d->extra) {
#if NDVIDEO_AUDIO_MODULES
                if (*codec_module_error()) d->error=codec_module_error();
#endif
                goto failed;
            }
            rate=d->extra->rate;channels=d->extra->channels;frames=d->extra->frames;
            stats->codec=d->extra->codec;
#else
            d->error="This audio codec is not included in this build";goto failed;
#endif
        } else {
#if NDMEDIA_WITH_MP3
        d->mp3_io = (mp3dec_io_t){mp3_read, d, mp3_seek, d};
        if (mp3dec_ex_open_cb(&d->mp3, &d->mp3_io, MP3D_DO_NOT_SCAN | MP3D_SEEK_TO_SAMPLE)) {
            mp3dec_ex_close(&d->mp3);
            goto failed;
        }
        d->mp3_open = true;
        rate = (unsigned)d->mp3.info.hz;
        channels = (unsigned)d->mp3.info.channels;
        frames = channels ? d->mp3.samples / channels : 0;
        stats->codec = 2;
#else
        d->error="MP3 is not included in this build";goto failed;
#endif
        }
    }
    if(!media_audio_codec_supported(stats->codec)) {
        d->error="This audio codec is not included in this build";goto failed;
    }
    if (d->io_failed || channels < 1 || channels > 2 || rate < 8000 || rate > 96000 ||
        frames > UINT32_MAX)
        goto failed;
    if (!capacity)
        capacity = 65536;
    if (capacity < 2048 || capacity > 131072)
        goto failed;
    s->source_bits = source_bits;
    s->pcm_bits = source_bits > 16 ? 32 : 16;
    s->pcm = malloc((size_t)capacity * channels * (s->pcm_bits / 8));
    if (!s->pcm)
        goto failed;
    s->sample_count = frames ? (uint32_t)frames : UINT32_MAX;
    s->sample_rate = rate;
    s->channels = channels;
    d->logical_frames = s->sample_count;
    s->pcm_capacity = capacity;
    d->error="Audio stream decoding failed";
    error[0] = 0;
    return d;
failed:
    snprintf(error, error_capacity, "%s",d->error);
    audio_decoder_close(d);
    audio_buffer_close(s);
    return NULL;
}
int audio_decoder_step(AudioDecoder *d, uint64_t consumed)
{
    AudioBuffer *s = d->audio;
    if (d->io_failed || s->failed) {
        d->error = "USB audio stream read/buffer failed";
        return -1;
    }
    uint32_t keep = (uint32_t)(consumed >> 32);
    if (keep)
        --keep;
    if (keep > s->pcm_end) {
        d->error = "Decoded audio buffer underrun";
        s->failed = true;
        return -1;
    }
    if (keep > s->pcm_begin)
        s->pcm_begin = keep;
    unsigned space = s->pcm_capacity - (s->pcm_end - s->pcm_begin);
    if (d->eof || space < 1024)
        return 0;
    unsigned count = 1024;
    if (count > s->sample_count - s->pcm_end)
        count = s->sample_count - s->pcm_end;
    uint32_t start = d->now(), read_before = d->stats->read_ms;
    uint64_t got;
    bool decode_failed=false;
    if (NDMEDIA_WITH_WAV && d->wav_open)
        got = s->pcm_bits == 32 ? drwav_read_pcm_frames_s32(&d->wav, count, d->block.s32)
                               : drwav_read_pcm_frames_s16(&d->wav, count, d->block.s16);
    else if (NDMEDIA_WITH_FLAC && d->flac)
        got = s->pcm_bits == 32 ? drflac_read_pcm_frames_s32(d->flac, count, d->block.s32)
                               : drflac_read_pcm_frames_s16(d->flac, count, d->block.s16);
    else if (d->extra) {
        unsigned filled=0;
        while (filled<count) {
            int n=d->extra->ops->read(d->extra,d->block.s16+(size_t)filled*s->channels,count-filled);
            if (n<0 || (unsigned)n>count-filled) {decode_failed=true;break;}
            if (!n) break;
            filled+=(unsigned)n;
        }
        got=filled;
    } else if (NDMEDIA_WITH_MP3)
        got = mp3dec_ex_read(&d->mp3, d->block.s16, (size_t)count * s->channels) / s->channels;
    else {got=0;decode_failed=true;}
    uint32_t elapsed = d->now() - start, read_delta = d->stats->read_ms - read_before;
    if (elapsed > d->max_step_ms)
        d->max_step_ms = elapsed;
    d->stats->decode_ms += elapsed > read_delta ? elapsed - read_delta : 0;
    ++d->steps;
    if (decode_failed || d->io_failed || got > count || (got < count && s->sample_count != UINT32_MAX)) {
        d->error = decode_failed ? (d->extra->error?d->extra->error:"Audio packet decode failed") :
            "Streamed audio ended early or could not be read";
        s->failed = true;
        return -1;
    }
    unsigned offset = s->pcm_end % s->pcm_capacity;
    unsigned first = s->pcm_capacity - offset;
    if (first > got)
        first = (unsigned)got;
    if (s->pcm_bits == 32) {
        /* Ndless int32_t is long, while the decoder libraries use int. Assign
         * samples rather than aliasing either type through the other's pointer. */
        int32_t *out = s->pcm;
        for (size_t i = 0; i < (size_t)first * s->channels; ++i)
            out[(size_t)offset * s->channels + i] = d->block.s32[i];
        for (size_t i = 0; i < ((size_t)got - first) * s->channels; ++i)
            out[i] = d->block.s32[(size_t)first * s->channels + i];
    } else {
        int16_t *out = s->pcm;
        memcpy(out + (size_t)offset * s->channels, d->block.s16,
               (size_t)first * s->channels * sizeof(*out));
        if (got > first)
            memcpy(out, d->block.s16 + (size_t)first * s->channels,
                   ((size_t)got - first) * s->channels * sizeof(*out));
    }
    s->pcm_end += (uint32_t)got;
    if (got < count || s->pcm_end == s->sample_count) {
        d->eof = true;
        s->sample_count = s->pcm_end;
        if (d->logical_frames==UINT32_MAX) d->logical_frames=s->pcm_end;
    }
    return got ? 1 : 0;
}
bool audio_decoder_eof(const AudioDecoder *d)
{
    return d->eof;
}
bool audio_decoder_limit(AudioDecoder *d, uint32_t frames)
{
    if (!frames || frames > d->audio->sample_count || d->audio->pcm_end)
        return false;
    d->logical_frames = d->audio->sample_count = frames;
    return true;
}
static bool seek_wav(AudioDecoder *d, uint32_t frame)
{
    drwav *wav = &d->wav;
    unsigned channels = wav->channels, block = wav->fmt.blockAlign;
    /* Each WAV IMA block carries its own predictor/index for every channel.
     * dr_wav's generic compressed seek decodes from the start on backwards
     * jumps. Start at the containing block instead, then let the same decoder
     * discard only the samples before the exact destination in that block.
     * Keep its generic path for formats/layouts outside this block contract. */
    if (wav->translatedFormatTag != DR_WAVE_FORMAT_DVI_ADPCM ||
        channels < 1 || channels > 2 || wav->bitsPerSample != 4 ||
        block < 4U * channels || (block - 4U * channels) % (4U * channels))
        return drwav_seek_to_pcm_frame(wav, frame) != 0;
    uint32_t frames_per_block = 1U + (block - 4U * channels) * 2U / channels;
    uint64_t block_index = frame / frames_per_block;
    uint64_t offset = block_index * block;
    if (offset >= wav->dataChunkDataSize ||
        wav->dataChunkDataSize - offset < 4U * channels ||
        wav->dataChunkDataPos > d->input.bytes ||
        offset > d->input.bytes - wav->dataChunkDataPos ||
        !seek_absolute(d, wav->dataChunkDataPos + offset))
        return false;
    memset(&wav->ima, 0, sizeof(wav->ima));
    wav->readCursorInPCMFrames = block_index * frames_per_block;
    wav->bytesRemaining = wav->dataChunkDataSize - offset;
    return drwav_seek_to_pcm_frame(wav, frame) != 0;
}
bool audio_decoder_seek(AudioDecoder *d, uint32_t frame)
{
    if (frame > d->logical_frames)
        return false;
    /* The decoder's encoded cursor is already at pcm_end. A destination in
     * the retained ring can play immediately without rereading that interval. */
    if (!d->io_failed && !d->audio->failed && frame >= d->audio->pcm_begin &&
        frame < d->audio->pcm_end)
        return true;
    bool ok = true;
    d->io_failed = false;
    if (frame < d->logical_frames) {
        if (NDMEDIA_WITH_WAV && d->wav_open)
            ok = seek_wav(d, frame);
        else if (NDMEDIA_WITH_FLAC && d->flac)
            ok = drflac_seek_to_pcm_frame(d->flac, frame) != 0;
        else if (d->extra)
            ok=d->extra->ops->seek(d->extra,frame);
        else if (NDMEDIA_WITH_MP3)
            ok = mp3dec_ex_seek(&d->mp3, (uint64_t)frame * d->audio->channels) == 0;
        else ok=false;
    }
    if (!ok || d->io_failed) {
        d->error = "Audio seek failed";
        return false;
    }
    AudioBuffer *s = d->audio;
    s->failed = s->pcm_missed = false;
    s->pcm_begin = s->pcm_end = frame;
    s->sample_count = d->logical_frames;
    d->eof = frame == d->logical_frames;
    return true;
}
const char *audio_decoder_error(const AudioDecoder *d)
{
    return d->error;
}
void audio_decoder_debug(const AudioDecoder *d, FILE *file)
{
    char text[256];
    if (d)
        describe(d, text, sizeof(text));
    fprintf(file, "stream_decoder %s\n", d ? text : last_stream_report);
}
