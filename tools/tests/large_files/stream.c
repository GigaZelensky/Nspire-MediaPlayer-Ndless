#include "player/movie_file.h"
#include "player/storage_read_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct StorageReadStream { uint64_t size; };
static struct StorageReadStream stream={1ULL<<50};
static unsigned reads;
static unsigned char pattern(uint64_t at){return (unsigned char)(at^(at>>8)^(at>>16)^(at>>24)^(at>>32)^(at>>40)^(at>>48)^(at>>56));}
StorageReadStream *storage_read_stream_open(const char *path,int *error){(void)path;*error=0;return &stream;}
uint64_t storage_read_stream_size(const StorageReadStream *file){return file->size;}
int storage_read_stream_read_at(StorageReadStream *file,uint64_t at,void *data,uint32_t bytes,int *error){
 assert(at<=file->size&&bytes<=file->size-at);*error=0;++reads;
 for(unsigned i=0;i<bytes;++i)((unsigned char*)data)[i]=pattern(at+i);
 return (int)bytes;
}
int storage_read_stream_close(StorageReadStream *file,int *error){assert(file==&stream);*error=0;return 0;}
void raw_player_before_native(void){}
void nve_wipe(void *p,size_t n){volatile unsigned char *b=p;while(n--)*b++=0;}
int main(void){
 MovieReadFile *file=movie_file_open("mock");assert(file&&movie_file_size(file)==(1ULL<<50));
 uint64_t positions[]={0,(1ULL<<31)-17,(1ULL<<31)+9,(1ULL<<32)-17,(1ULL<<32)+9,(1ULL<<36)-17,(1ULL<<36)+9,(1ULL<<39)+987,(1ULL<<47)+987};
 unsigned char data[5000];
 for(unsigned j=0;j<sizeof(positions)/sizeof(*positions);++j){
  assert(!movie_file_seek(file,positions[j]));assert(movie_file_read(data,1,sizeof(data),file)==sizeof(data));
  for(unsigned i=0;i<sizeof(data);++i)assert(data[i]==pattern(positions[j]+i));
 }
 assert(!movie_file_seek(file,100));reads=0;assert(movie_file_read(data,1,4,file)==4);assert(movie_file_read(data,1,4,file)==4);assert(reads==1);
 assert(!movie_file_seek(file,stream.size-7));assert(movie_file_read(data,2,10,file)==3);assert(movie_file_read(data,1,1,file)==0);
 assert(movie_file_seek(file,stream.size+1)==-1);assert(!movie_file_close(file));
 puts("PASS 64-bit buffered movie reads across 2/4/64 GiB, rewind/cache reuse, EOF and out-of-range seeks");
}
