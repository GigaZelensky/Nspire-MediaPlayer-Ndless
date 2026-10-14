#include "movie/nvp_validation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put32(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(n>>(i*8));}
static void put64(unsigned char *p,uint64_t n){put32(p,(uint32_t)n);put32(p+4,(uint32_t)(n>>32));}
int main(void){
 MovieHeader h={0},parsed;ChunkIndexEntry e={0},loaded;unsigned char wire[56]={0},index[28]={0};
 memcpy(h.magic,"NVP1",4);h.version=12;h.flags=2;h.canvas_width=320;h.canvas_height=240;h.video_width=320;h.video_height=180;h.fps_num=23980;h.fps_den=1000;h.frame_count=2;h.chunk_count=1;
 h.index_offset=(1ULL<<36)+128;
 memcpy(wire,&h,40);put64(wire+40,h.index_offset);put64(wire+48,0);
 assert(nvp_header_parse(&parsed,wire,56)&&parsed.index_offset==h.index_offset);assert(!nvp_header_parse(&parsed,wire,55));
 assert(nvp_header_parse(&parsed,wire,56)&&nvp_header_is_valid(&parsed,h.index_offset+28));
 assert(!nvp_header_is_valid(&parsed,h.index_offset+27));
 e.offset=1ULL<<36;e.packed_size=e.unpacked_size=128;e.frame_count=2;
 put64(index,e.offset);memcpy(index+8,&e.packed_size,20);nvp_index_parse(&loaded,index,12);
 assert(loaded.offset==e.offset&&nvp_index_is_valid(&h,&loaded));
 loaded.offset=h.index_offset+1;assert(!nvp_index_is_valid(&h,&loaded));
 for(unsigned version=9;version<=11;++version){
  h.version=version;h.index_offset=500;h.subtitle_offset=0;memcpy(wire,&h,40);put32(wire+40,500);put32(wire+44,0);
  assert(nvp_header_parse(&parsed,wire,48)&&parsed.index_offset==500&&nvp_header_is_valid(&parsed,524));
  e.offset=48;put32(index,48);memcpy(index+4,&e.packed_size,20);nvp_index_parse(&loaded,index,version);assert(nvp_index_is_valid(&h,&loaded));
 }
 puts("PASS wide NVP header/index parsing and bounds above 64 GiB; versions 9/10/11 remain readable");
}
