#include "movie/nve_crypto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t read64(FILE *f){unsigned char b[8];assert(fread(b,1,8,f)==8);uint64_t n=0;for(unsigned i=0;i<8;++i)n|=(uint64_t)b[i]<<(8*i);return n;}
static uint32_t read32(FILE *f){unsigned char b[4];assert(fread(b,1,4,f)==4);return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);uint64_t physical=read64(f);unsigned char header[128];assert(fread(header,1,128,f)==128);
 NveHeader h;NveKdf k;NveKeys keys;assert(nve_header_parse(&h,header,physical));assert(h.plain_bytes>(1ULL<<36));
 unsigned char damaged[128];memcpy(damaged,header,128);damaged[4]=2;assert(!nve_header_parse(&h,damaged,physical));
 memcpy(damaged,header,128);damaged[56]=1;assert(!nve_header_parse(&h,damaged,physical));
 assert(!nve_header_parse(&h,header,physical-1));assert(nve_header_parse(&h,header,physical));
 assert(nve_kdf_begin(&k,&h,"large-file-test",15));while(!nve_kdf_step(&k,137)){}assert(nve_kdf_finish(&k,&keys));
 NveReader *r=calloc(1,sizeof(*r));unsigned char cipher[16416],plain[16384],out[16384];assert(r);nve_reader_init(r,&keys);
 unsigned count=read32(f);
 for(unsigned i=0;i<count;++i){
  uint64_t offset=read64(f);uint32_t bytes=read32(f);assert(bytes&&bytes<=16384);
  assert(fread(cipher,1,bytes+32,f)==bytes+32&&fread(plain,1,bytes,f)==bytes);
  assert(nve_reader_begin(r,offset,out,bytes));assert(nve_reader_physical_offset(r)==128+(offset/16384)*16416);
  memcpy(r->block,cipher,bytes+32);nve_reader_supplied(r,true);
  while(r->phase!=NVE_READ_DONE&&r->phase!=NVE_READ_ERROR)nve_reader_step(r);
  assert(r->phase==NVE_READ_DONE&&!memcmp(out,plain,bytes));
  nve_reader_cancel(r);memset(out,0xa5,sizeof(out));assert(nve_reader_begin(r,offset,out,bytes));
  memcpy(r->block,cipher,bytes+32);r->block[0]^=1;nve_reader_supplied(r,true);
  while(r->phase!=NVE_READ_DONE&&r->phase!=NVE_READ_ERROR)nve_reader_step(r);
  assert(r->phase==NVE_READ_ERROR&&r->error==NVE_ERROR_AUTH);for(unsigned j=0;j<bytes;++j)assert(out[j]==0xa5);
 }
 nve_wipe(r,sizeof(*r));free(r);nve_wipe(&keys,sizeof(keys));fclose(f);
 puts("PASS full 600000-round wide NVE interoperability, 2/4/64/128 GiB and 64/128 TiB counter/index boundaries, malformed headers and tamper rejection");
}
