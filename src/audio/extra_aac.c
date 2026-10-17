#include "extra_audio.h"
#include <neaacdec.h>
#include <stdlib.h>
#include <string.h>
/* minimp4 also defines unused muxing helpers. Keep their strdup portable;
 * the linker discards them from this demux-only caller. */
static char *mp4_copy_string(const char *text)
{ size_t size=strlen(text)+1;char *copy=malloc(size);if(copy)memcpy(copy,text,size);return copy; }
#define strdup mp4_copy_string
#define MINIMP4_IMPLEMENTATION
#include "vendor/minimp4/minimp4.h"
#undef strdup
#include <stdio.h>

enum { AAC_CACHE_BYTES=16384, AAC_PACKET_LIMIT=65536, AAC_PCM_FRAMES=2048 };
typedef struct { uint64_t offset; uint32_t bytes; } AdtsPacket;
typedef struct {
    NeAACDecHandle decoder;
    MP4D_demux_t mp4;
    AdtsPacket *adts;
    uint32_t packets, capacity, packet, unit;
    unsigned track;
    bool mp4_open, io_failed;
    uint64_t trim, discard_until;
    uint64_t cache_offset;
    unsigned cache_bytes;
    uint8_t cache[AAC_CACHE_BYTES], encoded[AAC_PACKET_LIMIT];
    int16_t pcm[AAC_PCM_FRAMES*2];
    unsigned pcm_count, pcm_used;
} AacState;

static bool read_at(ExtraAudio *a,uint64_t offset,void *dst,size_t bytes)
{
    AacState *s=a->state;
    if (offset>a->input.bytes || bytes>a->input.bytes-offset) return false;
    while (bytes) {
        if (!s->cache_bytes || offset<s->cache_offset || offset-s->cache_offset>=s->cache_bytes) {
            unsigned count=a->input.bytes-offset>AAC_CACHE_BYTES?AAC_CACHE_BYTES:(unsigned)(a->input.bytes-offset);
            if (!a->input.seek(a->input.context,offset) ||
                a->input.read(a->input.context,s->cache,count)!=count) {
                s->io_failed=true;return false;
            }
            s->cache_offset=offset;s->cache_bytes=count;
        }
        size_t index=(size_t)(offset-s->cache_offset),count=s->cache_bytes-index;
        if (count>bytes) count=bytes;
        memcpy(dst,s->cache+index,count);
        dst=(uint8_t *)dst+count;offset+=count;bytes-=count;
    }
    return true;
}
static int mp4_read(int64_t offset,void *dst,size_t bytes,void *context)
{ return offset>=0 && read_at(context,(uint64_t)offset,dst,bytes)?0:-1; }
static uint32_t be32(const uint8_t *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static uint64_t be64(const uint8_t *p)
{ return (uint64_t)be32(p)<<32|be32(p+4); }

/* Read only a bounded box header. Metadata is skipped by its validated length,
 * not allocated or searched as an arbitrary byte string inside media data. */
typedef struct { uint64_t data,end,next; uint32_t type; } Box;
static bool box_next(ExtraAudio *a,uint64_t offset,uint64_t limit,Box *box)
{
    uint8_t p[16];
    if (offset>limit || limit-offset<8 || !read_at(a,offset,p,8)) return false;
    uint64_t size=be32(p);unsigned header=8;
    if (size==1) {if (limit-offset<16 || !read_at(a,offset+8,p+8,8)) return false;size=be64(p+8);header=16;}
    else if (!size) size=limit-offset;
    if (size<header || size>limit-offset) return false;
    *box=(Box){offset+header,offset+size,offset+size,be32(p+4)};
    return true;
}
#define BOX(a,b,c,d) ((uint32_t)(a)<<24|(uint32_t)(b)<<16|(uint32_t)(c)<<8|(d))
static bool edit_list(ExtraAudio *a,uint64_t *trim,uint64_t *duration)
{
    AacState *s=a->state;
    uint32_t track=0;
    Box top,trak,edts,elst;
    for (uint64_t pos=0;pos<a->input.bytes;pos=top.next) {
        if (!box_next(a,pos,a->input.bytes,&top)) return false;
        if (top.type!=BOX('m','o','o','v')) continue;
        for (uint64_t p=top.data;p<top.end;p=trak.next) {
            if (!box_next(a,p,top.end,&trak)) return false;
            if (trak.type!=BOX('t','r','a','k')) continue;
            if (track++!=s->track) continue;
            for (uint64_t q=trak.data;q<trak.end;q=edts.next) {
                if (!box_next(a,q,trak.end,&edts)) return false;
                if (edts.type!=BOX('e','d','t','s')) continue;
                for (uint64_t r=edts.data;r<edts.end;r=elst.next) {
                    if (!box_next(a,r,edts.end,&elst)) return false;
                    if (elst.type!=BOX('e','l','s','t')) continue;
                    uint8_t h[8],entry[20];
                    if (elst.end-elst.data<8 || !read_at(a,elst.data,h,8) || h[0]>1) return false;
                    unsigned width=h[0]?20:12;uint32_t count=be32(h+4);
                    if (count>(elst.end-elst.data-8)/width || count>64) return false;
                    bool selected=false;
                    for (uint32_t i=0;i<count;++i) {
                        if (!read_at(a,elst.data+8+(uint64_t)i*width,entry,width)) return false;
                        uint64_t segment=h[0]?be64(entry):be32(entry);
                        int64_t media=h[0]?(int64_t)be64(entry+8):(int32_t)be32(entry+4);
                        unsigned rate_offset=h[0]?16:8;
                        if (be32(entry+rate_offset)!=0x10000U) return false;
                        if (media<0) continue; /* Empty lead-in edit has no audio samples. */
                        if (selected || !s->mp4.timescale) return false;
                        uint32_t scale=s->mp4.track[s->track].timescale;
                        if (!scale || (uint64_t)media>UINT64_MAX/a->rate || segment>UINT64_MAX/a->rate) return false;
                        *trim=(uint64_t)media*a->rate/scale;
                        *duration=segment*a->rate/s->mp4.timescale;
                        selected=true;
                    }
                    return selected;
                }
            }
            return true; /* No edit: use the decoder's one-frame priming delay. */
        }
    }
    return true;
}

static bool adts_index(ExtraAudio *a)
{
    AacState *s=a->state;
    uint8_t h[10];uint64_t offset=0;
    if (!read_at(a,0,h,10)) return false;
    if (!memcmp(h,"ID3",3)) {
        if ((h[6]|h[7]|h[8]|h[9])&128) return false;
        offset=10+((uint32_t)h[6]<<21)+((uint32_t)h[7]<<14)+((uint32_t)h[8]<<7)+h[9];
        if (h[5]&16) offset+=10;
    }
    while (offset<a->input.bytes) {
        if (a->input.bytes-offset==128 && read_at(a,offset,h,3) && !memcmp(h,"TAG",3)) break;
        if (a->input.bytes-offset<7 || !read_at(a,offset,h,7) || h[0]!=255 || (h[1]&0xf6)!=0xf0 ||
            (h[6]&3)) return false; /* One raw data block per ADTS packet. */
        unsigned size=((unsigned)(h[3]&3)<<11)|((unsigned)h[4]<<3)|(h[5]>>5);
        if (size<(h[1]&1?7U:9U) || size>a->input.bytes-offset) return false;
        if (s->packets==s->capacity) {
            uint32_t capacity=s->capacity?s->capacity*2:1024;
            if (capacity<s->capacity || capacity>UINT32_MAX/sizeof(*s->adts)) return false;
            AdtsPacket *next=realloc(s->adts,(size_t)capacity*sizeof(*next));
            if (!next) return false;
            s->adts=next;s->capacity=capacity;
        }
        s->adts[s->packets++]=(AdtsPacket){offset,size};offset+=size;
    }
    return s->packets>1;
}
static bool packet_info(ExtraAudio *a,uint32_t packet,uint64_t *offset,unsigned *bytes,uint64_t *position)
{
    AacState *s=a->state;
    if (packet>=s->packets) return false;
    if (s->mp4_open) {
        unsigned timestamp,duration;
        *offset=MP4D_frame_offset(&s->mp4,s->track,packet,bytes,&timestamp,&duration);
        uint32_t scale=s->mp4.track[s->track].timescale;
        if (!scale) return false;
        *position=(uint64_t)timestamp*a->rate/scale;
    } else {
        *offset=s->adts[packet].offset;*bytes=s->adts[packet].bytes;
        *position=(uint64_t)packet*s->unit;
    }
    return *bytes && *bytes<=AAC_PACKET_LIMIT && *offset<=a->input.bytes && *bytes<=a->input.bytes-*offset;
}
static bool decoder_init(ExtraAudio *a)
{
    AacState *s=a->state;
    if (s->decoder) NeAACDecClose(s->decoder);
    s->decoder=NeAACDecOpen();
    if (!s->decoder) return false;
    NeAACDecConfigurationPtr config=NeAACDecGetCurrentConfiguration(s->decoder);
    config->defObjectType=LC;config->outputFormat=FAAD_FMT_16BIT;config->downMatrix=1;
    if (!NeAACDecSetConfiguration(s->decoder,config)) return false;
    unsigned long rate;unsigned char channels;
    if (s->mp4_open) {
        MP4D_track_t *track=&s->mp4.track[s->track];
        char result=NeAACDecInit2(s->decoder,track->dsi,track->dsi_bytes,&rate,&channels);
#ifdef NDVIDEO_EXTRA_AUDIO_TRACE
        if (result) {
            fprintf(stderr,"AAC ASC init returned %d dsi_bytes=%u dsi=",result,track->dsi_bytes);
            for (unsigned i=0;i<track->dsi_bytes;++i) fprintf(stderr,"%02x",track->dsi[i]);
            fprintf(stderr,"\n");
        }
#endif
        if (result) return false;
    } else {
        unsigned bytes=s->adts[0].bytes;
        if (!read_at(a,s->adts[0].offset,s->encoded,bytes) ||
            NeAACDecInit(s->decoder,s->encoded,bytes,&rate,&channels)<0) return false;
    }
    return true;
}
static bool decode_packet(ExtraAudio *a,uint32_t packet,NeAACDecFrameInfo *info,void **pcm)
{
    AacState *s=a->state;uint64_t offset,position;unsigned bytes;
    if (!packet_info(a,packet,&offset,&bytes,&position) || !read_at(a,offset,s->encoded,bytes)) return false;
    *pcm=NeAACDecDecode(s->decoder,info,s->encoded,bytes);
    if (info->error || (info->samples && !*pcm) || info->bytesconsumed>bytes) {
        a->error=info->error?NeAACDecGetErrorMessage(info->error):"AAC packet decode failed";
        return false;
    }
    return true;
}
static bool seek_aac(ExtraAudio *a,uint32_t frame)
{
    AacState *s=a->state;
    if (frame>a->frames) return false;
    uint64_t target=s->trim+frame;
    uint32_t begin=0,end=s->packets;
    while (begin+1<end) {
        uint32_t mid=begin+(end-begin)/2;uint64_t offset,pos;unsigned bytes;
        if (!packet_info(a,mid,&offset,&bytes,&pos)) return false;
        if (pos>target) end=mid;else begin=mid;
    }
    /* Warm overlap/SBR state before exposing the exact requested sample. */
    s->packet=begin>16?begin-16:0;s->discard_until=target;
    s->pcm_count=s->pcm_used=0;
    return decoder_init(a);
}
static int read_aac(ExtraAudio *a,int16_t *pcm,unsigned frames)
{
    AacState *s=a->state;unsigned written=0;
    while (written<frames) {
        if (s->pcm_used<s->pcm_count) {
            unsigned count=s->pcm_count-s->pcm_used;
            if (count>frames-written) count=frames-written;
            memcpy(pcm+(size_t)written*a->channels,s->pcm+(size_t)s->pcm_used*a->channels,
                   (size_t)count*a->channels*2);
            written+=count;s->pcm_used+=count;continue;
        }
        if (s->packet>=s->packets) break;
        uint64_t offset,pos;unsigned bytes;
        if (!packet_info(a,s->packet,&offset,&bytes,&pos)) return -1;
        NeAACDecFrameInfo info;void *decoded;
        if (!decode_packet(a,s->packet++,&info,&decoded)) return -1;
        if (!info.samples) continue;
        if (info.channels!=a->channels || info.samplerate!=a->rate ||
            info.samples%a->channels || info.samples/a->channels>AAC_PCM_FRAMES) return -1;
        unsigned count=(unsigned)(info.samples/a->channels),skip=0;
        if (pos<s->discard_until) {
            uint64_t discard=s->discard_until-pos;
            skip=discard>count?count:(unsigned)discard;
        }
        if (pos+count>s->trim+a->frames)
            count=pos>=s->trim+a->frames?0:(unsigned)(s->trim+a->frames-pos);
        if (skip>=count) continue;
        s->pcm_count=count-skip;s->pcm_used=0;
        memcpy(s->pcm,(int16_t *)decoded+(size_t)skip*a->channels,(size_t)s->pcm_count*a->channels*2);
    }
    return (int)written;
}
static void close_aac(ExtraAudio *a)
{
    AacState *s=a->state;
    if (!s) return;
    if (s->decoder) NeAACDecClose(s->decoder);
    if (s->mp4_open) MP4D_close(&s->mp4);
    free(s->adts);free(s);
}
static const ExtraAudioOps ops={read_aac,seek_aac,close_aac};
ExtraAudio *extra_aac_open(AudioInput input)
{
    const char *stage="signature";
    ExtraAudio *a=calloc(1,sizeof(*a));AacState *s=calloc(1,sizeof(*s));
    if (!a || !s) {free(a);free(s);return NULL;}
    *a=(ExtraAudio){.input=input,.ops=&ops,.state=s,.codec=5};
    uint8_t signature[8];
    if (!read_at(a,0,signature,8)) goto fail;
    if (!memcmp(signature+4,"ftyp",4) || !memcmp(signature+4,"moov",4) || !memcmp(signature+4,"free",4)) {
        stage="MP4 index";
        if (input.bytes>INT64_MAX || !MP4D_open(&s->mp4,mp4_read,a,(int64_t)input.bytes)) goto fail;
        s->mp4_open=true;
        stage="AAC track selection";
        for (s->track=0;s->track<s->mp4.track_count;++s->track) {
            MP4D_track_t *track=&s->mp4.track[s->track];
            if (track->handler_type==MP4D_HANDLER_TYPE_SOUN && track->object_type_indication==0x40 &&
                track->dsi_bytes && track->dsi_bytes<=64) break;
        }
        if (s->track==s->mp4.track_count) goto fail;
        s->packets=s->mp4.track[s->track].sample_count;
        if (s->packets<2) goto fail;
    } else if (!adts_index(a)) goto fail;
    stage="decoder initialization";
    if (!decoder_init(a)) goto fail;
    a->rate=44100;s->unit=1024; /* Packet offsets do not depend on these probe values. */
    stage="initial packets";
    for (unsigned i=0;i<3 && i<s->packets;++i) {
        NeAACDecFrameInfo info;void *pcm;
        if (!decode_packet(a,i,&info,&pcm)) goto fail;
        if (!info.samples) continue;
        if (!info.channels || info.samples%info.channels) goto fail;
        a->rate=(uint32_t)info.samplerate;a->channels=info.channels;
        s->unit=(uint32_t)(info.samples/info.channels);break;
    }
    if (a->channels<1 || a->channels>2 || a->rate<8000 || a->rate>96000 ||
        !s->unit || s->unit>AAC_PCM_FRAMES) goto fail;
    stage="duration";
    s->trim=s->unit;
    uint64_t raw_frames=(uint64_t)s->packets*s->unit,duration;
    if (s->mp4_open) {
        MP4D_track_t *track=&s->mp4.track[s->track];
        uint64_t ticks=(uint64_t)track->duration_hi<<32|track->duration_lo;
        if (!track->timescale || ticks>UINT64_MAX/a->rate) goto fail;
        raw_frames=ticks*a->rate/track->timescale;
    }
    duration=raw_frames>s->trim?raw_frames-s->trim:0;
    stage="edit list";
    if (s->mp4_open && !edit_list(a,&s->trim,&duration)) goto fail;
    stage="timeline bounds";
    if (!duration || duration>UINT32_MAX || s->trim>raw_frames || duration>raw_frames-s->trim) goto fail;
    a->frames=(uint32_t)duration;
    stage="initial seek";
    if (!seek_aac(a,0)) goto fail;
    return a;
fail:
#ifdef NDVIDEO_EXTRA_AUDIO_TRACE
    fprintf(stderr,"AAC open failed at %s: packets=%lu rate=%lu ch=%u unit=%lu trim=%llu error=%s\n",
            stage,(unsigned long)s->packets,(unsigned long)a->rate,a->channels,(unsigned long)s->unit,
            (unsigned long long)s->trim,a->error?a->error:"none");
#else
    (void)stage;
#endif
    close_aac(a);free(a);return NULL;
}
