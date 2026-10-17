#include "movie/nve_crypto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv)
{
    assert(argc==3);
    FILE *input=fopen(argv[1],"rb"),*output=fopen(argv[2],"wb");assert(input && output);
    assert(fseek(input,0,SEEK_END)==0);long size=ftell(input);assert(size>128);
    assert(fseek(input,0,SEEK_SET)==0);
    unsigned char wire[128],plain[16384];assert(fread(wire,1,128,input)==128);
    NveHeader header;NveKdf kdf;NveKeys keys;
    assert(nve_header_parse(&header,wire,(uint64_t)size) && header.codec_hint==2);
    assert(header.plain_bytes<8*1024*1024);
    assert(nve_kdf_begin(&kdf,&header,"media-test",10));
    while(!nve_kdf_step(&kdf,4096)) {}
    assert(nve_kdf_finish(&kdf,&keys));
    NveReader *reader=calloc(1,sizeof(*reader));assert(reader);nve_reader_init(reader,&keys);
    for(uint64_t offset=0;offset<header.plain_bytes;) {
        unsigned count=header.plain_bytes-offset>sizeof(plain)?sizeof(plain):(unsigned)(header.plain_bytes-offset);
        assert(nve_reader_begin(reader,offset,plain,count));
        assert(fseek(input,(long)nve_reader_physical_offset(reader),SEEK_SET)==0);
        unsigned bytes=reader->unit_bytes+NVE_TAG_BYTES;
        assert(fread(reader->block,1,bytes,input)==bytes);nve_reader_supplied(reader,true);
        while(reader->phase!=NVE_READ_DONE && reader->phase!=NVE_READ_ERROR)nve_reader_step(reader);
        assert(reader->phase==NVE_READ_DONE && fwrite(plain,1,count,output)==count);
        nve_reader_cancel(reader);offset+=count;
    }
    nve_wipe(reader,sizeof(*reader));free(reader);nve_wipe(&keys,sizeof(keys));
    assert(!fclose(input) && !fclose(output));
    puts("PASS encrypted media packing and C reader authentication");return 0;
}
