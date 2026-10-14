#include "player/movie_file.h"
#include "player/storage_read_stream.h"
#include "movie/nvp_validation.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
struct StorageReadStream {FILE *file;uint64_t bytes;};
StorageReadStream *storage_read_stream_open(const char *path,int *error){
 StorageReadStream *s=calloc(1,sizeof(*s));assert(s);s->file=fopen(path,"rb");assert(s->file);assert(!_fseeki64(s->file,0,SEEK_END));s->bytes=_ftelli64(s->file);*error=0;return s;
}
uint64_t storage_read_stream_size(const StorageReadStream *s){return s->bytes;}
int storage_read_stream_read_at(StorageReadStream *s,uint64_t offset,void *data,uint32_t bytes,int *error){
 *error=0;assert(!_fseeki64(s->file,(__int64)offset,SEEK_SET));return (int)fread(data,1,bytes,s->file);
}
int storage_read_stream_close(StorageReadStream *s,int *error){*error=0;int result=fclose(s->file);free(s);return result;}
void raw_player_before_native(void){}
void nve_wipe(void *p,size_t n){volatile unsigned char *b=p;while(n--)*b++=0;}
int main(int argc,char **argv){
 assert(argc==2);MovieReadFile *f=movie_file_open(argv[1]);assert(f&&movie_file_size(f)>(1ULL<<32));
 unsigned char raw[56];MovieHeader h;assert(movie_file_read(raw,1,56,f)==56&&nvp_header_parse(&h,raw,56));
 assert(nvp_header_is_valid(&h,movie_file_size(f)));assert(!movie_file_seek(f,h.index_offset));
 ChunkIndexEntry entries[2];assert(h.chunk_count==2);
 for(unsigned i=0;i<2;++i){assert(movie_file_read(raw,1,28,f)==28);nvp_index_parse(entries+i,raw,12);}
 assert(nvp_index_is_valid(&h,entries));
 for(unsigned i=0;i<2;++i){unsigned char payload[128];assert(!movie_file_seek(f,entries[i].offset));assert(movie_file_read(payload,1,128,f)==128);
  for(unsigned j=0;j<128;++j)assert(payload[j]==(unsigned char)(j+i*37));
 }
 assert(!movie_file_close(f));puts("PASS actual sparse file: wide header/index loading and payload seeks beyond 2 and 4 GiB through the movie stream API");
}
