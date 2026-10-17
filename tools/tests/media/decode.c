#include "audio/audio_decoder.h"
#include "movie/nvp_audio.h"
#include "movie/nvp_validation.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE *file;
    uint64_t start, bytes, cursor, read_bytes;
    unsigned read_calls, seek_calls;
    bool fail_seek;
} Input;
static size_t read_audio(void *context, void *data, size_t size)
{
    Input *in = context;
    if (size > in->bytes - in->cursor)
        size = (size_t)(in->bytes - in->cursor);
    assert(fseek(in->file, (long)(in->start + in->cursor), SEEK_SET) == 0);
    size_t got = fread(data, 1, size, in->file);
    in->cursor += got;
    in->read_bytes += got;
    ++in->read_calls;
    return got;
}
static bool seek_audio(void *context, uint64_t position)
{
    Input *in = context;
    ++in->seek_calls;
    if (position > in->bytes || in->fail_seek)
        return false;
    in->cursor = position;
    return true;
}
static uint64_t tell_audio(void *context)
{
    return ((Input *)context)->cursor;
}
static uint32_t now(void)
{
    return 0;
}
static unsigned drain(AudioDecoder *decoder, AudioBuffer *buffer, uint32_t frame, unsigned count,
                      int16_t *output)
{
    while (buffer->pcm_end < frame + count && !audio_decoder_eof(decoder)) {
        int step = audio_decoder_step(decoder, (uint64_t)frame << 32);
        if (step <= 0)
            fprintf(stderr, "decode stopped at %lu: %s\n", (unsigned long)frame,
                    audio_decoder_error(decoder));
        assert(step > 0);
    }
    assert(buffer->pcm_end >= frame + count);
    for (unsigned i = 0; i < count; ++i) {
        int16_t sample[2];
        audio_buffer_channels(buffer, (uint64_t)(frame + i) << 32, sample);
        for (unsigned ch = 0; ch < buffer->channels; ++ch)
            output[i * buffer->channels + ch] = sample[ch];
    }
    assert(!buffer->failed);
    return count;
}
int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 3);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    unsigned char wire[56], footer[48];
    MovieHeader header;
    NvpAudioTrack track;
    assert(fread(wire, 1, 56, file) == 56 && nvp_header_parse(&header, wire, 56));
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 104);
    assert(fseek(file, -48, SEEK_END) == 0 && fread(footer, 1, 48, file) == 48);
    assert(nvp_audio_parse(&track, footer, 48, (uint64_t)length, &header));
    Input in = {.file=file, .start=track.offset, .bytes=track.bytes};
    AudioInput input = {&in, read_audio, seek_audio, tell_audio, track.bytes};
    AudioBuffer buffer;
    AudioDecodeStats stats;
    char error[160];
    AudioDecoder *decoder =
        audio_decoder_open(&buffer, input, &stats, now, 4096, error, sizeof(error));
    if (!decoder) fprintf(stderr,"Open failed: %s\n",error);
    else fprintf(stderr,"Opened codec=%u rate=%u ch=%u samples=%lu requested=%lu\n",stats.codec,
                 buffer.sample_rate,buffer.channels,(unsigned long)buffer.sample_count,(unsigned long)track.samples);
    assert(decoder && audio_decoder_limit(decoder, track.samples));
    assert(buffer.sample_rate == track.rate && buffer.channels == track.channels &&
           stats.codec == track.codec);
    if (track.codec == 2) {
        /* Logical zero must preserve gapless delay without indexing the movie's
         * entire soundtrack. The previous path reread tens of MB at startup. */
        uint64_t before = in.read_bytes;
        assert(audio_decoder_seek(decoder, 0));
        assert(in.read_bytes == before);
        assert(in.read_bytes <= 262144);
    }
    int16_t *reference = malloc((size_t)track.samples * track.channels * 2);
    assert(reference);
    for (uint32_t frame = 0; frame < track.samples;) {
        unsigned n = track.samples - frame > 517 ? 517 : track.samples - frame;
        frame += drain(decoder, &buffer, frame, n, reference + (size_t)frame * track.channels);
    }
    assert(audio_decoder_eof(decoder) && buffer.pcm_end == track.samples);
    if (argc==3) {
        FILE *dump=fopen(argv[2],"wb");assert(dump);
        assert(fwrite(reference,2,(size_t)track.samples*track.channels,dump)==(size_t)track.samples*track.channels);
        assert(!fclose(dump));
    }
    /* A buffered destination keeps the encoded cursor/ring/EOF state intact.
     * It must reproduce samples without reading the source again. */
    uint32_t retained_begin=buffer.pcm_begin, retained_end=buffer.pcm_end;
    assert(retained_end>retained_begin);
    unsigned retained_count=retained_end-retained_begin;
    if(retained_count>514)retained_count=514;
    uint64_t encoded_cursor=in.cursor;
    assert(audio_decoder_seek(decoder,retained_end-retained_count));
    int16_t retained[514*2];
    drain(decoder,&buffer,retained_end-retained_count,retained_count,retained);
    assert(in.cursor==encoded_cursor && buffer.pcm_begin==retained_begin && buffer.pcm_end==retained_end);
    assert(!memcmp(retained,reference+(size_t)(retained_end-retained_count)*track.channels,retained_count*track.channels*2));
    /* Seek within ADPCM blocks, across MP3 reservoir/index boundaries, after
     * EOF and backwards. Compare to actual sequential decoding, sample-wise. */
    const uint32_t positions[] = {
        0, 1, 1031, 11027, 60000, 23000,
        track.samples / 2,
        track.samples > 100000 ? track.samples - 100000 : 0,
        0
    };
    int16_t samples[514 * 2];
    for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
        uint32_t frame = positions[i];
        if (frame + 514 >= track.samples)
            continue;
        assert(audio_decoder_seek(decoder, frame));
        drain(decoder, &buffer, frame, 514, samples);
        if (track.codec==NVP_AUDIO_OPUS) {
            /* libopusfile documents that its 80 ms seek preroll converges to
             * the original state rather than promising bit-identical PCM.
             * The backend also verifies op_pcm_tell equals the exact target.
             * Bound the remaining waveform difference, including near silence. */
            uint64_t error_energy=0,signal_energy=0;unsigned maximum=0;
            for (unsigned j=0;j<514*track.channels;++j) {
                int expected=reference[(size_t)frame*track.channels+j];
                int diff=(int)samples[j]-expected;
                error_energy+=(uint64_t)((int64_t)diff*diff);
                signal_energy+=(uint64_t)((int64_t)expected*expected);
                unsigned magnitude=(unsigned)(diff<0?-diff:diff);
                if (magnitude>maximum) maximum=magnitude;
            }
            fprintf(stderr,"Opus seek sample %lu max_delta=%u error_energy=%llu signal_energy=%llu\n",
                   (unsigned long)frame,maximum,(unsigned long long)error_energy,(unsigned long long)signal_energy);
            if (error_energy>signal_energy/10000+514*track.channels*4) {
                for(unsigned k=0;k<10;++k)fprintf(stderr,"%d/%d ",samples[k],reference[(size_t)frame*track.channels+k]);
                fputc('\n',stderr);
            }
            assert(error_energy<=signal_energy/10000+514*track.channels*4);
        } else {
            assert(!memcmp(samples,reference+(size_t)frame*track.channels,514*track.channels*2));
        }
    }
    if (track.codec == 1) {
        /* Read the independent WAV block size from its fmt chunk. A seek may
         * decode at most its destination block, regardless of song position.
         * The old replay-from-start implementation fails this byte budget. */
        uint64_t offset=12;
        unsigned block=0;
        while (offset+8<=track.bytes) {
            unsigned char chunk[24];
            assert(fseek(file,(long)(track.offset+offset),SEEK_SET)==0);
            assert(fread(chunk,1,8,file)==8);
            uint32_t bytes=(uint32_t)chunk[4]|(uint32_t)chunk[5]<<8|
                           (uint32_t)chunk[6]<<16|(uint32_t)chunk[7]<<24;
            if (!memcmp(chunk,"fmt ",4)) {
                assert(bytes>=16 && fread(chunk+8,1,16,file)==16);
                block=(unsigned)chunk[20]|(unsigned)chunk[21]<<8;
                break;
            }
            offset+=8+(uint64_t)bytes+(bytes&1);
        }
        assert(block>=4U*track.channels && block<65536);
        unsigned block_frames=1+(block-4U*track.channels)*2/track.channels;
        uint32_t random=719, largest_seek=0;
        for (unsigned i=0;i<72;++i) {
            random=random*1664525U+1013904223U;
            uint32_t frame=i<12 ? (i/3+1)*block_frames+(i%3)-1 :
                random%(track.samples-514);
            if (frame+514>=track.samples)continue;
            uint64_t before=in.read_bytes;
            assert(audio_decoder_seek(decoder,frame));
            uint64_t seek_bytes=in.read_bytes-before;
            if(seek_bytes>largest_seek)largest_seek=(uint32_t)seek_bytes;
            assert(seek_bytes<=block);
            drain(decoder,&buffer,frame,514,samples);
            assert(!memcmp(samples,reference+(size_t)frame*track.channels,
                           514*track.channels*2));
        }
        /* A failed source reposition is reported; a later retry can recover
         * without retaining stale predictor/cache state from the old block. */
        assert(audio_decoder_seek(decoder,track.samples));
        in.fail_seek=true;
        assert(!audio_decoder_seek(decoder,0));
        in.fail_seek=false;
        assert(audio_decoder_seek(decoder,1));
        drain(decoder,&buffer,1,514,samples);
        assert(!memcmp(samples,reference+track.channels,514*track.channels*2));
        printf("IMA random/block-boundary seeks: max %lu source bytes, block %u\n",
               (unsigned long)largest_seek,block);
    }
    assert(audio_decoder_seek(decoder, track.samples));
    assert(audio_decoder_eof(decoder));
    assert(!audio_decoder_seek(decoder, track.samples + 1));
    free(reference);
    audio_decoder_close(decoder);
    audio_buffer_close(&buffer);
    fclose(file);
    printf("PASS %s: exact EOF, ring wrap and sample-accurate seeks\n", argv[1]);
    return 0;
}
