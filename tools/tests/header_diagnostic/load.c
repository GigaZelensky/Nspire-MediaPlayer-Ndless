#include "player/header_diagnostic.h"
#include "player/movie_file.h"
#include "movie/nvp_validation.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
struct MovieReadFile {uint8_t data[56];unsigned position,calls,fail_on,partial;};
typedef struct {MovieReadFile *file;MovieHeader header;bool encrypted;} MovieTest;
static const char *stage;
static unsigned recorded,expected;
static bool valid;
uint64_t movie_file_size(const MovieReadFile *f){(void)f;return 1024;}
size_t movie_file_read(void *p,size_t size,size_t count,MovieReadFile *f)
{
    assert(size==1);++f->calls;
    size_t n=f->calls==f->fail_on?f->partial:count;
    if(n!=count)errno=EIO;
    assert(n<=count && n<=sizeof(f->data)-f->position);
    memcpy(p,f->data+f->position,n);f->position+=(unsigned)n;return n;
}
static void debug_failf(const char *text,...){(void)text;}
void header_diagnostic_result(const char *s,uint64_t bytes,bool crypt,const uint8_t *p,
    size_t received,size_t wanted,int error,bool ok)
{(void)p;(void)error;assert(bytes==1024 && !crypt);stage=s;recorded=(unsigned)received;expected=(unsigned)wanted;valid=ok;}
static bool read_header(MovieTest *movie)
{
    uint64_t file_size=0;
    /* PRODUCTION_HEADER_READ */
    (void)file_size;return true;
fail:return false;
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    for(unsigned i=0;i<4;++i) {
        struct MovieReadFile file={0};assert(fread(file.data,1,56,f)==56);
        unsigned wire=nvp_header_wire_bytes(file.data[4]);MovieTest movie={.file=&file};
        assert(read_header(&movie) && valid && recorded==wire && expected==wire && !strcmp(stage,"header valid"));
        file.position=file.calls=0;file.fail_on=1;file.partial=0;
        assert(!read_header(&movie) && !valid && !recorded && expected==48 && !strcmp(stage,"header read"));
        file.position=file.calls=0;file.partial=5;
        assert(!read_header(&movie) && recorded==5 && !strcmp(stage,"header read"));
        file.position=file.calls=0;file.fail_on=2;file.partial=0;
        if(wire==56)assert(!read_header(&movie) && recorded==48 && expected==56 && !strcmp(stage,"extended header read"));
        else assert(read_header(&movie));
        file.position=file.calls=file.fail_on=0;file.data[0]^=1;
        assert(!read_header(&movie) && !valid && !strcmp(stage,"invalid header or bounds"));
    }
    fclose(f);puts("PASS actual header-load block: v9/v11/v12/v13, initial and partial read failures, extension failures and malformed headers");return 0;
}
