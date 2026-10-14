#include "nve_crypto.h"
#include "nve_accel.h"
#include <limits.h>
#include <string.h>

static const char header_domain[] = "NVE1-header";
static const char block_domain[] = "NVE1-block";
static const char encryption_domain[] = "NVE1-encryption";
static const char authentication_domain[] = "NVE1-authentication";

void nve_wipe(void *data, size_t bytes)
{
    typedef uint32_t WipeWord __attribute__((__may_alias__));
    uint8_t *p = data;
    while (bytes && ((uintptr_t)p & 3U)) { *(volatile uint8_t *)p++ = 0; --bytes; }
    while (bytes >= 4U) { *(volatile WipeWord *)(void *)p = 0; p += 4; bytes -= 4; }
    while (bytes--) *(volatile uint8_t *)p++ = 0;
}
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t *p){return le32(p)|(uint64_t)le32(p+4)<<32;}
static void put32(uint8_t *p, uint32_t x)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(x >> (i * 8));
}
static bool equal_tag(const uint8_t *a, const uint8_t *b)
{
    uint32_t difference = 0;
    for (unsigned i = 0; i < NVE_TAG_BYTES; ++i) difference |= a[i] ^ b[i];
    return difference == 0;
}
static void key_prepare(br_hmac_key_context *soft,uint8_t raw[64],const void *key,size_t bytes)
{
    memset(raw,0,64);
    if(bytes>64){br_sha256_context hash;br_sha256_init(&hash);br_sha256_update(&hash,key,bytes);br_sha256_out(&hash,raw);nve_wipe(&hash,sizeof(hash));}
    else memcpy(raw,key,bytes);
    br_hmac_key_init(soft,&br_sha256_vtable,key,bytes);
}
static bool hmac_parts(const br_hmac_key_context *key,const uint8_t raw[64],const NveHashPart *parts,unsigned count,uint8_t out[32],int batch)
{
    int hardware=batch>=0?batch:nve_accel_begin();
    if(hardware<0)return false;
    bool ok=true;
    if(hardware)ok=nve_accel_hmac(raw,parts,count,out)>0;
    else {br_hmac_context h;br_hmac_init(&h,key,32);for(unsigned i=0;i<count;++i)br_hmac_update(&h,parts[i].data,parts[i].bytes);br_hmac_out(&h,out);nve_wipe(&h,sizeof(h));}
    if(batch<0&&hardware)ok=nve_accel_end()&&ok;
    if(!ok)nve_wipe(out,32);
    return ok;
}
static bool hmac(const br_hmac_key_context *key,const uint8_t raw[64],const void *data,size_t bytes,uint8_t out[32],int batch)
{
    NveHashPart part={data,bytes};return hmac_parts(key,raw,&part,1,out,batch);
}
static void kdf_failed(NveKdf *kdf){nve_wipe(kdf,sizeof(*kdf));kdf->failed=true;}
bool nve_header_parse(NveHeader *header, const void *data, uint64_t physical_bytes)
{
    if (!header || !data) return false;
    const uint8_t *p = data;
    uint32_t version = le32(p + 4);
    if (memcmp(p, "NVE1", 4) || (version != 1U && version != 2U && version != 3U) || le32(p + 8) != NVE_RECORD_BYTES ||
        le32(p + 12) != NVE_KDF_ROUNDS || !le32(p + 28) ||
        !le32(p + 60) || le32(p + 60) > 65535U || !le32(p + 64) || le32(p + 64) > 65535U) return false;
    if (version == 1U ? p[68] != 0 : p[68] > 4U || (version == 2U && p[68] < 1U)) return false;
    if (version == 3U && le32(p+56)) return false;
    for (unsigned i = 69; i < NVE_HEADER_AUTH_BYTES; ++i) if (p[i]) return false;
    uint64_t plain = le64(p + 16);
    uint64_t records = plain / NVE_RECORD_BYTES + (plain % NVE_RECORD_BYTES != 0);
    uint64_t overhead = NVE_HEADER_BYTES + records * NVE_TAG_BYTES;
    if (physical_bytes > INT64_MAX || plain < 48U || plain > UINT64_MAX - overhead ||
        (version < 3U && plain > (1ULL << 36)) || plain + overhead != physical_bytes) return false;
    memcpy(header->bytes, data, NVE_HEADER_BYTES);
    header->plain_bytes = plain;
    header->physical_bytes = physical_bytes;
    header->duration_ms = le32(p + 24);
    header->frame_count = le32(p + 28);
    header->fps_num = le32(p + 60);
    header->fps_den = le32(p + 64);
    header->codec_hint = p[68];
    return true;
}
bool nve_kdf_begin(NveKdf *kdf, const NveHeader *header, const void *password, size_t bytes)
{
    if (!kdf || !header || !password || !bytes || bytes > NVE_PASSWORD_MAX) return false;
    nve_wipe(kdf, sizeof(*kdf));
    kdf->header = *header;
    key_prepare(&kdf->password,kdf->password_key,password,bytes);
    uint8_t salt[20];
    memcpy(salt, header->bytes + 32, 16);
    salt[16] = salt[17] = salt[18] = 0; salt[19] = 1;
    if(!hmac(&kdf->password,kdf->password_key,salt,sizeof(salt),kdf->u,-1)){nve_wipe(salt,sizeof(salt));kdf_failed(kdf);return false;}
    memcpy(kdf->master, kdf->u, 32);
    kdf->rounds = 1;
    kdf->active = true;
    nve_wipe(salt, sizeof(salt));
    return true;
}
bool nve_kdf_step(NveKdf *kdf,unsigned rounds)
{
    if(!kdf)return false;
    if(kdf->failed)return true;
    if(!kdf->active)return false;
    int hardware=nve_accel_begin();
    if(hardware<0){kdf_failed(kdf);return true;}
    bool ok=true;
    while(rounds--&&kdf->rounds<NVE_KDF_ROUNDS){
        if(!hmac(&kdf->password,kdf->password_key,kdf->u,32,kdf->u,hardware)){ok=false;break;}
        for(unsigned i=0;i<32;++i)kdf->master[i]^=kdf->u[i];
        ++kdf->rounds;
    }
    if(hardware)ok=nve_accel_end()&&ok;
    if(!ok){kdf_failed(kdf);return true;}
    return kdf->rounds==NVE_KDF_ROUNDS;
}
bool nve_kdf_finish(NveKdf *kdf,NveKeys *keys)
{
    if(!kdf||!keys||!kdf->active||kdf->rounds!=NVE_KDF_ROUNDS)return false;
    br_hmac_key_context master;uint8_t raw[64],encryption[32],authentication[32],check[32];
    nve_wipe(keys,sizeof(*keys));key_prepare(&master,raw,kdf->master,32);
    bool ok=hmac(&master,raw,encryption_domain,sizeof(encryption_domain),encryption,-1)&&
        hmac(&master,raw,authentication_domain,sizeof(authentication_domain),authentication,-1);
    if(ok){
        key_prepare(&keys->mac,keys->mac_key,authentication,32);
        const NveHashPart parts[]={{header_domain,sizeof(header_domain)},{kdf->header.bytes,NVE_HEADER_AUTH_BYTES}};
        ok=hmac_parts(&keys->mac,keys->mac_key,parts,2,check,-1);
    }
    bool valid=ok&&equal_tag(check,kdf->header.bytes+NVE_HEADER_AUTH_BYTES);
    if(valid){keys->header=kdf->header;br_aes_ct_ctr_init(&keys->aes,encryption,sizeof(encryption));keys->valid=true;}
    else nve_wipe(keys,sizeof(*keys));
    nve_wipe(raw,sizeof(raw));nve_wipe(encryption,sizeof(encryption));nve_wipe(authentication,sizeof(authentication));
    nve_wipe(check,sizeof(check));nve_wipe(&master,sizeof(master));nve_wipe(kdf,sizeof(*kdf));kdf->failed=!ok;
    return valid;
}
void nve_reader_init(NveReader *reader, const NveKeys *keys)
{
    memset(reader, 0, sizeof(*reader));
    reader->keys = keys;
    reader->cached_unit = UINT64_MAX;
}
static void select_unit(NveReader *r)
{
    r->unit = (r->offset + r->copied) / NVE_RECORD_BYTES;
    uint64_t remaining = r->keys->header.plain_bytes - r->unit * NVE_RECORD_BYTES;
    r->unit_bytes = remaining > NVE_RECORD_BYTES ? NVE_RECORD_BYTES : (uint32_t)remaining;
    r->progress = 0;
    r->phase = r->cached_unit == r->unit ? NVE_READ_COPY : NVE_READ_FETCH;
}
bool nve_reader_begin(NveReader *r, uint64_t offset, void *destination, uint32_t bytes)
{
    if (!r || !r->keys || !r->keys->valid || !destination || !bytes ||
        offset > r->keys->header.plain_bytes || bytes > r->keys->header.plain_bytes - offset ||
        (uintptr_t)destination > UINTPTR_MAX - bytes)
        return false;
    if (r->phase != NVE_READ_DONE && r->phase != NVE_READ_IDLE) nve_reader_cancel(r);
    r->offset = offset; r->bytes = bytes; r->copied = 0; r->destination = destination;
    r->error = NVE_ERROR_NONE;
    select_unit(r);
    return true;
}
uint64_t nve_reader_physical_offset(const NveReader *r)
{
    return NVE_HEADER_BYTES + r->unit * (NVE_RECORD_BYTES + NVE_TAG_BYTES);
}
static void reader_error(NveReader *r, NveReadError error)
{
    NveReadPhase phase = r->phase;
    if (error != NVE_ERROR_AUTH) {
        memset(r->error_cipher_hash, 0, sizeof(r->error_cipher_hash));
        memset(r->error_tag, 0, sizeof(r->error_tag));
    }
    nve_reader_cancel(r);
    r->error = error;
    r->error_phase = phase;
    r->phase = NVE_READ_ERROR;
}

static unsigned record_index(const NveReader *r,uint8_t index[12])
{
    put32(index,(uint32_t)r->unit);
    if(le32(r->keys->header.bytes+4)==3U){put32(index+4,(uint32_t)(r->unit>>32));put32(index+8,r->unit_bytes);return 12;}
    put32(index+4,r->unit_bytes);return 8;
}
void nve_reader_supplied(NveReader *r, bool success)
{
    if (!r || r->phase != NVE_READ_FETCH) return;
    r->cached_unit = UINT64_MAX;
    if (!success) { reader_error(r, NVE_ERROR_IO); return; }
    uint8_t index[12];
    unsigned index_bytes=record_index(r,index);
    br_hmac_init(&r->mac, &r->keys->mac, 32);
    br_hmac_update(&r->mac, block_domain, sizeof(block_domain));
    br_hmac_update(&r->mac, r->keys->header.bytes + NVE_HEADER_AUTH_BYTES, NVE_TAG_BYTES);
    br_hmac_update(&r->mac, index, index_bytes);
    r->progress = 0; r->phase = NVE_READ_VERIFY;
}
void nve_reader_step(NveReader *r)
{
    if (!r) return;
    if (!r->keys || !r->keys->valid) {
        reader_error(r, NVE_ERROR_KEYS); return;
    }
    if ((r->phase == NVE_READ_VERIFY || r->phase == NVE_READ_DECRYPT) &&
        (!r->unit_bytes || r->unit_bytes > NVE_RECORD_BYTES || r->progress >= r->unit_bytes)) {
        reader_error(r, NVE_ERROR_STATE); return;
    }
    if (r->phase == NVE_READ_VERIFY) {
        uint8_t tag[32];bool have_tag=false;
        if(!r->progress){
            int hardware=nve_accel_begin();
            if(hardware<0){reader_error(r,NVE_ERROR_ACCELERATOR);return;}
            if(hardware){
                uint8_t index[12];unsigned index_bytes=record_index(r,index);
                const NveHashPart parts[]={{block_domain,sizeof(block_domain)},
                    {r->keys->header.bytes+NVE_HEADER_AUTH_BYTES,NVE_TAG_BYTES},{index,index_bytes},{r->block,r->unit_bytes}};
                bool ok=nve_accel_hmac(r->keys->mac_key,parts,4,tag)>0;
                ok=nve_accel_end()&&ok;
                if(!ok){nve_wipe(tag,sizeof(tag));reader_error(r,NVE_ERROR_ACCELERATOR);return;}
                r->progress=r->unit_bytes;have_tag=true;
            }
        }
        if(!have_tag){
            uint32_t count=r->unit_bytes-r->progress;if(count>512U)count=512U;
            br_hmac_update(&r->mac,r->block+r->progress,count);r->progress+=count;
            if(r->progress==r->unit_bytes)br_hmac_out(&r->mac,tag);
        }
        if (r->progress == r->unit_bytes) {
            bool valid = equal_tag(tag, r->block + r->unit_bytes);
            nve_wipe(tag, sizeof(tag)); nve_wipe(&r->mac, sizeof(r->mac));
            if (!valid) {
                /* Fingerprint ciphertext and its public tag, never plaintext
                 * or key material, before the rejected record is wiped. */
                br_sha256_context hash;
                br_sha256_init(&hash);
                br_sha256_update(&hash, r->block, r->unit_bytes);
                br_sha256_out(&hash, r->error_cipher_hash);
                memcpy(r->error_tag, r->block + r->unit_bytes, NVE_TAG_BYTES);
                nve_wipe(&hash, sizeof(hash));
                reader_error(r, NVE_ERROR_AUTH); return;
            }
            r->progress = 0; r->phase = NVE_READ_DECRYPT;
        }
    } else if (r->phase == NVE_READ_DECRYPT) {
        uint32_t count = r->unit_bytes - r->progress;
        if (count > 64U) count = 64U;
        uint64_t counter=(r->unit * NVE_RECORD_BYTES + r->progress) / 16U;
        if(le32(r->keys->header.bytes+4)==3U){
            uint8_t nonce[12];memcpy(nonce,r->keys->header.bytes+48,8);
            uint32_t high=(uint32_t)(counter>>32);
            for(unsigned i=0;i<4;++i)nonce[8+i]=(uint8_t)(high>>(24-8*i));
            br_aes_ct_ctr_run(&r->keys->aes,nonce,(uint32_t)counter,r->block+r->progress,count);
            nve_wipe(nonce,sizeof(nonce));
        } else br_aes_ct_ctr_run(&r->keys->aes,r->keys->header.bytes+48,
            (uint32_t)counter,r->block+r->progress,count);
        r->progress += count;
        if (r->progress == r->unit_bytes) { r->cached_unit = r->unit; r->phase = NVE_READ_COPY; }
    } else if (r->phase == NVE_READ_COPY) {
        uint32_t within = (r->offset + r->copied) % NVE_RECORD_BYTES;
        if (!r->destination || r->cached_unit != r->unit || r->copied >= r->bytes ||
            !r->unit_bytes || r->unit_bytes > NVE_RECORD_BYTES || within >= r->unit_bytes) {
            reader_error(r, NVE_ERROR_STATE); return;
        }
        uint32_t count = r->bytes - r->copied;
        if (count > r->unit_bytes - within) count = r->unit_bytes - within;
        if (count > 512U) count = 512U;
        memcpy(r->destination + r->copied, r->block + within, count);
        r->copied += count;
        if (r->copied == r->bytes) r->phase = NVE_READ_DONE;
        else if (within + count == r->unit_bytes) select_unit(r);
    }
}
void nve_reader_cancel(NveReader *r)
{
    if (!r) return;
    nve_wipe(r->block, sizeof(r->block)); nve_wipe(&r->mac, sizeof(r->mac));
    r->cached_unit = UINT64_MAX; r->phase = NVE_READ_IDLE; r->destination = NULL;
}
