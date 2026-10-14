#include "sha_accel.h"
#include "native_interrupts.h"
#include "../movie/nve_accel.h"
#include "../movie/nve_crypto.h"
#include "../platform/nspire_hardware.h"
#include "../platform/hardware_acceleration.h"
#include "../platform/portable_reader_platform.h"
#include <string.h>
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define SHA 0xcc000000U
static PortableReaderPlatform mapping;
static struct {
    uint32_t mask, gate_address, gate, leases, hmacs, hashes, blocks, failures, busy, clock;
    uint32_t word_blocks, short_hmacs;
    unsigned error;
    bool active, verified;
} state;
static void barrier(void) { __asm__ volatile("mcr p15,0,%0,c7,c10,4"::"r"(0):"memory"); }
static bool mapped(uint32_t address) {
    uint32_t e=REG(mapping.table_alias+(address>>20)*4U);
    return (e&0x1ebU)==2U && (e&0xfff00000U)==(address&0xfff00000U) && (e&0xc00U);
}
static bool idle(void) {
    /* Bounded independently of the player's/native timer configuration. */
    for(unsigned i=0;i<65536U;++i)if(!(REG(SHA)&0x800U))return true;
    state.error=3;return false;
}
static uint32_t read_be_word(const uint8_t *p) {
    if(!((uintptr_t)p&3U)){typedef uint32_t Word __attribute__((__may_alias__));return __builtin_bswap32(*(const Word *)(const void *)p);}
    return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}
static void write_be_word(uint8_t *p,uint32_t v) {p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static bool digest(const NveHashPart *parts,unsigned count,uint8_t out[32]) {
    size_t bytes=0;
    for(unsigned i=0;i<count;++i){if(parts[i].bytes>16U*1024U*1024U-bytes||(!parts[i].data&&parts[i].bytes)){state.error=4;return false;}bytes+=parts[i].bytes;}
    REG(SHA)=16;barrier();REG(SHA)=0;barrier();
    size_t total=(bytes+9U+63U)&~(size_t)63U;unsigned part=0;size_t within=0;
    for(size_t at=0;at<total;at+=64){
        while(part<count&&within==parts[part].bytes){++part;within=0;}
        /* Feed complete blocks directly; only fragmented/padded blocks need
         * the byte assembler. Unaligned input stays safe on ARM926. */
        if(at<bytes&&bytes-at>=64U&&part<count&&parts[part].bytes-within>=64U) {
            const uint8_t *source=(const uint8_t*)parts[part].data+within;
            for(unsigned word=0;word<16;++word)REG(SHA+16+word*4)=read_be_word(source+word*4);
            within+=64U;++state.word_blocks;
        } else for(unsigned word=0;word<16;++word){
            uint32_t v=0;
            for(unsigned byte=0;byte<4;++byte){
                size_t pos=at+word*4+byte;uint8_t b=0;
                if(pos<bytes){while(part<count&&within==parts[part].bytes){++part;within=0;}
                    b=((const uint8_t*)parts[part].data)[within++];}
                else if(pos==bytes)b=0x80;
                if(pos>=total-8)b=(uint8_t)(((uint64_t)bytes*8U)>>((total-1-pos)*8));
                v=(v<<8)|b;
            }
            REG(SHA+16+word*4)=v;
        }
        barrier();REG(SHA)=at?14:10;barrier();++state.blocks;if(!idle())return false;
    }
    for(unsigned i=0;i<8;++i){uint32_t v=REG(SHA+96+i*4);for(unsigned j=0;j<4;++j)out[i*4+j]=(uint8_t)(v>>(24-j*8));}
    return true;
}
/* Cache packed pad words only during the exclusive hardware lease. These
 * are key material, so end() erases them before restoring interrupts. */
static struct {uint8_t key[64];uint32_t inner[16],outer[16];bool valid;} words_cache;
static bool word_block(const uint32_t words[16],bool first) {
    for(unsigned i=0;i<16;++i)REG(SHA+16+i*4)=words[i];
    barrier();REG(SHA)=first?10:14;barrier();++state.blocks;++state.word_blocks;return idle();
}
static void reset_engine(void) {REG(SHA)=16;barrier();REG(SHA)=0;barrier();}
static bool hmac_word32(const uint8_t key[64],const uint8_t *message,uint8_t out[32]) {
    if(!words_cache.valid||memcmp(words_cache.key,key,64)) {
        memcpy(words_cache.key,key,64);
        for(unsigned i=0;i<16;++i){uint32_t word=read_be_word(key+i*4);words_cache.inner[i]=word^0x36363636U;words_cache.outer[i]=word^0x5c5c5c5cU;}
        words_cache.valid=true;
    }
    uint32_t block[16]={0};for(unsigned i=0;i<8;++i)block[i]=read_be_word(message+i*4);
    block[8]=0x80000000U;block[15]=768; /* pad + message: 96 bytes. */
    reset_engine();bool ok=word_block(words_cache.inner,true)&&word_block(block,false);
    if(ok){for(unsigned i=0;i<8;++i)block[i]=REG(SHA+96+i*4);
        reset_engine();ok=word_block(words_cache.outer,true)&&word_block(block,false);}
    if(ok)for(unsigned i=0;i<8;++i)write_be_word(out+i*4,REG(SHA+96+i*4));
    nve_wipe(block,sizeof(block));return ok;
}
bool nve_accel_end(void) {
    if(!state.active)return state.error==0;
    REG(SHA)=16;barrier();REG(SHA)=0;barrier();
    if(!idle())++state.failures;
    else {for(unsigned i=0;i<16;++i)REG(SHA+16+i*4)=0;barrier();}
    REG(state.gate_address)=state.gate;barrier();state.active=false;
    nve_wipe(&words_cache,sizeof(words_cache));
    native_critical_leave(state.mask);
    return state.error==0;
}
int nve_accel_begin(void) {
    if(!hardware_acceleration_enabled(HW_ACCEL_SHA))return 0;
    uint32_t asic=nspire_hardware_id();
    if(asic!=0x202U&&!nspire_asic_is_cx(asic))return 0;
    if(state.active){state.error=5;++state.failures;return -1;}
    state.mask=native_critical_enter();
    state.gate_address=asic==0x202U?0x90140050U:0x900b0018U;
    if(!portable_reader_platform_refresh(&mapping)||!mapped(SHA)||!mapped(state.gate_address)){
        state.error=1;++state.failures;native_critical_leave(state.mask);return -1;
    }
    state.gate=REG(state.gate_address);REG(state.gate_address)=state.gate&~(1U<<10);barrier();
    if(REG(SHA)&0x800U){
        REG(state.gate_address)=state.gate;barrier();state.error=2;++state.busy;++state.failures;
        native_critical_leave(state.mask);return -1;
    }
    state.active=true;state.error=0;++state.leases;
    uint32_t clock=REG(asic==0x202U?0x90140030U:0x900b0024U);
    if(clock!=state.clock)state.verified=false;
    if(!state.verified){
        static const uint8_t expected[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
        uint8_t out[32];NveHashPart part={"abc",3};
        bool ok=digest(&part,1,out)&&!memcmp(out,expected,32);nve_wipe(out,sizeof(out));
        if(!ok){state.error=6;++state.failures;nve_accel_end();return -1;}
        if(ok){
            static const uint8_t long_expected[32]={0xff,0xba,0xd8,0xf9,0x47,0x47,0x4c,0xfd,0xd5,0xb2,0xbb,0x22,0xd7,0xe0,0xbf,0x5e,0xe8,0xba,0x2b,0x7a,0xf8,0x59,0xd0,0xc2,0xbb,0x28,0x62,0x2d,0xb6,0xa4,0xbe,0x47};
            uint8_t pattern[1024];for(unsigned i=0;i<sizeof(pattern);++i)pattern[i]=(uint8_t)(i*37U+11U);
            part=(NveHashPart){pattern,sizeof(pattern)};
            ok=digest(&part,1,out)&&!memcmp(out,long_expected,32);
            nve_wipe(pattern,sizeof(pattern));nve_wipe(out,sizeof(out));
            if(!ok){state.error=6;++state.failures;nve_accel_end();return -1;}
        }
        /* Exercise the PBKDF2 fast path as well as the generic digest path. */
        static const uint8_t hmac_expected[32]={0x63,0x05,0x47,0x5e,0xe9,0xb9,0xdf,0x9c,0x90,0xcf,0xfb,0x97,0xe7,0x5f,0x1c,0x6e,0x32,0xca,0x4a,0xfc,0x1f,0xb6,0x7a,0xca,0xf4,0x12,0x1c,0xc7,0xd9,0x29,0xd7,0x43};
        uint8_t key[64]={0},message[32];
        for(unsigned i=0;i<32;++i){key[i]=(uint8_t)i;message[i]=(uint8_t)(i*37U+11U);}
        ok=hmac_word32(key,message,out)&&!memcmp(out,hmac_expected,32);
        nve_wipe(key,sizeof(key));nve_wipe(message,sizeof(message));nve_wipe(out,sizeof(out));
        nve_wipe(&words_cache,sizeof(words_cache));
        if(!ok){state.error=6;++state.failures;nve_accel_end();return -1;}
        state.verified=true;state.clock=clock;
    }
    return 1;
}
int nve_accel_hmac(const uint8_t key[64],const NveHashPart *parts,unsigned count,uint8_t out[32]) {
    if(!state.active||count>4){state.error=4;++state.failures;return -1;}
    size_t bytes=0;for(unsigned i=0;i<count;++i){if(parts[i].bytes>32768U-bytes){state.error=4;++state.failures;return -1;}bytes+=parts[i].bytes;}
    /* PBKDF2 repeatedly authenticates its previous 32-byte digest. Reuse
     * pad words and pass the inner digest directly between hardware rounds. */
    if(count==1&&parts[0].bytes==32&&parts[0].data) {
        if(!hmac_word32(key,parts[0].data,out)){++state.failures;nve_wipe(out,32);return -1;}
        ++state.hmacs;++state.short_hmacs;return 1;
    }
    uint8_t pad[64],inner[32];NveHashPart input[5];
    for(unsigned i=0;i<64;++i)pad[i]=key[i]^0x36;
    input[0]=(NveHashPart){pad,64};for(unsigned i=0;i<count;++i)input[i+1]=parts[i];
    bool ok=digest(input,count+1,inner);
    if(ok){for(unsigned i=0;i<64;++i)pad[i]=key[i]^0x5c;
        input[1]=(NveHashPart){inner,32};ok=digest(input,2,out);}
    nve_wipe(pad,sizeof(pad));nve_wipe(inner,sizeof(inner));
    if(!ok){++state.failures;nve_wipe(out,32);return -1;}
    ++state.hmacs;return 1;
}
int nve_accel_hash(const NveHashPart *parts,unsigned count,uint8_t out[32]) {
    if(!state.active||count>5||!digest(parts,count,out)){
        if(!state.error)state.error=4;
        ++state.failures;nve_wipe(out,32);return -1;
    }
    ++state.hashes;return 1;
}
void sha_accel_debug(FILE *file) {
    if(file)fprintf(file,"sha_acceleration model=%08lx verified=%u leases=%lu hmacs=%lu blocks=%lu failures=%lu busy=%lu error=%u active=%u hashes=%lu verified_clock=%08lx word_blocks=%lu short_hmacs=%lu\n",
        (unsigned long)nspire_hardware_id(),state.verified,(unsigned long)state.leases,(unsigned long)state.hmacs,
        (unsigned long)state.blocks,(unsigned long)state.failures,(unsigned long)state.busy,state.error,state.active,(unsigned long)state.hashes,(unsigned long)state.clock,(unsigned long)state.word_blocks,(unsigned long)state.short_hmacs);
}
