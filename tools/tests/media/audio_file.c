#include "audio/audio_decoder.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {FILE *file;uint64_t bytes,position;} File;
static size_t read_file(void *context,void *dst,size_t bytes)
{File *f=context;size_t n=fread(dst,1,bytes,f->file);f->position+=n;return n;}
static bool seek_file(void *context,uint64_t position)
{File *f=context;if(position>f->bytes || position>0x7fffffff || fseek(f->file,(long)position,SEEK_SET))return false;f->position=position;return true;}
static uint64_t tell_file(void *context) {return ((File *)context)->position;}
static uint32_t now(void){return 0;}
int main(int argc,char **argv)
{
    assert(argc==2 || argc==3);
    bool reject=argc==3 && !strcmp(argv[1],"--reject");
    const char *path=reject?argv[2]:argv[1];
    FILE *file=fopen(path,"rb");assert(file);
    assert(!fseek(file,0,SEEK_END));long size=ftell(file);assert(size>0 && !fseek(file,0,SEEK_SET));
    File in={file,(uint64_t)size,0};AudioInput input={&in,read_file,seek_file,tell_file,in.bytes};
    AudioBuffer buffer;AudioDecodeStats stats;char error[160];
    AudioDecoder *decoder=audio_decoder_open(&buffer,input,&stats,now,4096,error,sizeof(error));
    if(reject) {assert(!decoder);audio_buffer_close(&buffer);fclose(file);return 0;}
    if(!decoder)fprintf(stderr,"%s: %s\n",argv[1],error);
    assert(decoder);
    FILE *out=argc==3?fopen(argv[2],"w+b"):NULL;if(argc==3)assert(out);
    uint32_t position=0,expected=buffer.sample_count;
    for(;;) {
        int step=audio_decoder_step(decoder,(uint64_t)position<<32);
        if(step<0)fprintf(stderr,"%s at %lu: %s\n",argv[1],(unsigned long)position,audio_decoder_error(decoder));
        assert(step>=0);
        while(position<buffer.pcm_end) {
            int16_t sample[2];audio_buffer_channels(&buffer,(uint64_t)position<<32,sample);
            assert(!buffer.failed);
            if(out)assert(fwrite(sample,2,buffer.channels,out)==buffer.channels);
            ++position;
        }
        if(audio_decoder_eof(decoder))break;
        assert(step>0);
    }
    assert(position && (expected==UINT32_MAX || expected==position));
    assert(buffer.sample_count==position);
    if(out && position>1024) {
        assert(!fflush(out));
        uint32_t targets[]={0,1,1031,position/2,position-1000,0};
        for(unsigned i=0;i<sizeof(targets)/sizeof(*targets);++i) {
            uint32_t at=targets[i];if(at+514>position)continue;
            assert(audio_decoder_seek(decoder,at));
            while(buffer.pcm_end<at+514)assert(audio_decoder_step(decoder,(uint64_t)at<<32)>0);
            int16_t reference[514*2];
            assert(!fseek(out,(long)((uint64_t)at*buffer.channels*2),SEEK_SET));
            assert(fread(reference,2,514*buffer.channels,out)==514*buffer.channels);
            uint64_t error_energy=0,signal_energy=0;
            for(unsigned j=0;j<514;++j) {
                int16_t sample[2];audio_buffer_channels(&buffer,(uint64_t)(at+j)<<32,sample);
                for(unsigned ch=0;ch<buffer.channels;++ch) {
                    int expected_sample=reference[j*buffer.channels+ch],diff=sample[ch]-expected_sample;
                    error_energy+=(uint64_t)((int64_t)diff*diff);
                    signal_energy+=(uint64_t)((int64_t)expected_sample*expected_sample);
                }
            }
            /* libopusfile's full seeks use convergent preroll, not guaranteed
             * identical decoder state. Its adapter checks exact op_pcm_tell;
             * check PCM equivalence on the deterministic regression fixture. */
            if(stats.codec!=6)assert(!error_energy);
        }
    }
    assert(!audio_decoder_seek(decoder,position+1));
    assert(audio_decoder_seek(decoder,0));
    assert(audio_decoder_step(decoder,0)>0);
    if(out)assert(!fclose(out));
    printf("{\"codec\":%u,\"rate\":%u,\"channels\":%u,\"frames\":%lu,\"bytes\":%lu}\n",
        stats.codec,(unsigned)buffer.sample_rate,buffer.channels,(unsigned long)position,(unsigned long)in.bytes);
    audio_decoder_close(decoder);audio_buffer_close(&buffer);assert(!fclose(file));
    return 0;
}
