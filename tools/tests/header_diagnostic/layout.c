#include "storage/flashfx_region.h"
#include "storage/flashfx_metadata.h"
#include "movie/nvp_validation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Header bytes and protected tag from the two independently captured failing
 * CX II pages. The remaining bytes were erased. No movie content is needed. */
static const char *headers[]={
"ccdd444c5f4653342e3030ffffffffff000019030d000000750000007402000009030000f3000000010000081000000040003d003f00d60b",
"ccdd444c5f4653342e3030ffffffffff008037030e0000007500000072020000090300009a020000010000081000000040003d003f001c0c"};
static void half(unsigned char *p,unsigned n){p[0]=n;p[1]=n>>8;}
static void page(unsigned char *p,unsigned which,unsigned column)
{
    memset(p,255,2112);
    for(unsigned i=0;i<56;++i){unsigned v;assert(sscanf(headers[which]+2*i,"%2x",&v)==1);p[i]=v;}
    memcpy(p+column,"\xe2\x48\x55\x05",4);
}
static void state(unsigned char *s,unsigned units)
{
    memset(s,0,2688);memset(s+538,255,2048);
    half(s,64);half(s+20,2048);half(s+32,units);half(s+40,63);half(s+536,1);
    for(unsigned i=0;i<units;++i)half(s+538+2*i,0);
}
static unsigned run(unsigned initial,unsigned physical)
{
    unsigned char s[2688],p[2112];state(s,2);FfxRegionLoader r;
    assert(ffx_region_begin_layout(&r,s,sizeof(s),1,0,initial));unsigned tags=0;
    for(unsigned steps=0;steps<1000 && r.status!=FFX_REGION_DONE;++steps){
        assert(ffx_region_step(&r)!=FFX_REGION_ERROR);
        if(!r.pending)continue;
        if(r.request.bytes==2112)page(p,r.cursor-1,physical);
        else {assert(r.request.bytes==4 && r.request.column==physical);memset(p,255,4);++tags;}
        assert(ffx_region_supply(&r,r.request.token,p,r.request.bytes));
    }
    assert(r.status==FFX_REGION_DONE && tags==126 && r.tag_column==physical);
    return r.tag_column;
}
static void reject(unsigned initial,unsigned corruption)
{
    unsigned char s[2688],p[2112];state(s,1);FfxRegionLoader r;
    assert(ffx_region_begin_layout(&r,s,sizeof(s),1,0,initial));assert(ffx_region_step(&r)==FFX_REGION_NEED_PAGE);
    page(p,0,2080);
    if(corruption==1)p[54]^=1; /* checksum */
    if(corruption==2)p[0]^=1; /* signature */
    if(corruption==3)p[2052]=0; /* +4 isn't erased; do not override it */
    if(corruption==4)p[2080]=0; /* protected-tag corruption */
    assert(!ffx_region_supply(&r,r.request.token,p,2112) && r.error==7);
}
int main(void)
{
    /* OS-read header from the same failed file. It also declares an index
     * beyond the native file size: keep that independent bounds rejection. */
    const char *wire="4e5650310a0000004001f000000021004001ae00e02ee80300008206b9360100e80000003f0b0000606212032078120329f5030000000000";
    unsigned char h[56];MovieHeader movie;
    for(unsigned i=0;i<56;++i){unsigned v;assert(sscanf(wire+2*i,"%2x",&v)==1);h[i]=v;}
    assert(nvp_header_parse(&movie,h,sizeof(h)));
    assert(movie.index_offset==51536480 && movie.subtitle_offset==51542048);
    assert(!nvp_header_is_valid(&movie,46755840));
    assert(nvp_header_is_valid(&movie,60000000));
    unsigned char p[2112];unsigned seq;
    for(unsigned i=0;i<2;++i){page(p,i,2080);assert(!ffx_decode_unit_header_layout(p,2112,&seq,2052));
        assert(ffx_decode_unit_header_layout(p,2112,&seq,2080));assert(seq==(i?626:628));}
    run(2052,2080);run(2052,2052);run(2060,2060);
    for(unsigned i=1;i<=4;++i)reject(2052,i);
    reject(2060,0); /* Classic CX never changes ECC/tag layout. */
    unsigned char s[2688];state(s,2);FfxRegionLoader r;
    assert(ffx_region_begin_layout(&r,s,sizeof(s),1,0,2052));
    assert(ffx_region_step(&r)==FFX_REGION_NEED_PAGE);page(p,0,2080);
    assert(ffx_region_supply(&r,r.request.token,p,2112));
    assert(ffx_region_step(&r)==FFX_REGION_NEED_PAGE);page(p,1,2052);
    assert(!ffx_region_supply(&r,r.request.token,p,2112) && r.error==7);
    puts("PASS captured CX II pages, +32 tag routing, existing +4/+12 layouts, checksum/signature/tag rejection and consistent unit layout");
    return 0;
}
