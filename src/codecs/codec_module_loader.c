#if NDVIDEO_CODEC_MODULES || NDVIDEO_AUDIO_MODULES
#include "codec_module_api.h"
#include "codec_module_format.h"
#include "crypto/bearssl/bearssl_hash.h"
#include "movie/nve_accel.h"
#include "platform/hardware_acceleration.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* Ndless runs ARM code from its ordinary heap after cache synchronization. */
void clear_cache(void);
typedef struct {
    uint32_t id, offset, bytes, image_bytes;
    uint8_t digest[32];
} ModuleRecord;
typedef struct {
    void *allocation;
    uint8_t *image;
    size_t image_bytes;
    size_t allocation_bytes;
    unsigned references;
    CodecModuleApi api;
} LoadedModule;
static LoadedModule loaded[CODEC_MODULE_COUNT];
static char bundle_path[1024], last_error[192];
static char integrity_details[768];
static const CodecHostApi *host_api;
static bool loader_busy;
static unsigned active_codec = CODEC_MODULE_COUNT;
static uint8_t expected_hashes[CODEC_MODULE_COUNT][32];
static uint32_t expected_mask;
static CodecModuleProgress progress_callback;
static void *progress_userdata;

static bool fail(const char *message)
{
    snprintf(last_error, sizeof(last_error), "%s", message);
    return false;
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void write32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static bool read_at(FILE *file, uint32_t offset, void *buffer, size_t bytes)
{
    return !fseek(file, (long)offset, SEEK_SET) && fread(buffer, 1, bytes, file) == bytes;
}

static bool progress(size_t done, size_t total)
{
    return !progress_callback || progress_callback(progress_userdata, done, total) ||
           fail("Codec module loading cancelled");
}

static void software_digest(const NveHashPart *parts,unsigned count,uint8_t hash[32])
{
    br_sha256_context context;
    br_sha256_init(&context);
    for(unsigned i=0;i<count;++i)br_sha256_update(&context,parts[i].data,parts[i].bytes);
    br_sha256_out(&context, hash);
}
static int hash_parts(const NveHashPart *parts,unsigned count,uint8_t hash[32])
{
    int hardware=nve_accel_begin();
    if(hardware<0){fail("Hardware SHA unavailable while verifying codec");return -1;}
    if(!hardware){software_digest(parts,count,hash);return 0;}
    bool ok=nve_accel_hash(parts,count,hash)>0;
    ok=nve_accel_end()&&ok;
    if(!ok){fail("Hardware SHA failed while verifying codec");return -1;}
    return 1;
}
static void hash_hex(const uint8_t hash[32],char text[65])
{
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32;++i){text[i*2]=digits[hash[i]>>4];text[i*2+1]=digits[hash[i]&15];}text[64]=0;
}
static void diagnose_payload(const ModuleRecord *record,const NveHashPart parts[3],const uint8_t actual[32],int hardware)
{
    uint8_t resident[32],second[32],scratch[4096];char wanted[65],got[65],memory[65],again[65];
    software_digest(parts,3,resident);
    uint32_t difference=UINT32_MAX;unsigned loaded_byte=0,read_byte=0;bool read_ok=false;
    FILE *check=fopen(bundle_path,"rb");br_sha256_context context;br_sha256_init(&context);
    if(check){
        read_ok=true;
        for(uint32_t pos=0;pos<record->bytes;){
            uint32_t bytes=record->bytes-pos;if(bytes>sizeof(scratch))bytes=sizeof(scratch);
            if(!progress(pos,record->bytes)||!read_at(check,record->offset+pos,scratch,bytes)){read_ok=false;break;}
            br_sha256_update(&context,scratch,bytes);
            if(difference==UINT32_MAX)for(unsigned i=0;i<bytes;++i){
                uint32_t at=pos+i;unsigned part=0;
                while(part<2&&at>=parts[part].bytes){at-=parts[part].bytes;++part;}
                unsigned value=((const uint8_t*)parts[part].data)[at];
                if(value!=scratch[i]){difference=pos+i;loaded_byte=value;read_byte=scratch[i];break;}
            }
            pos+=bytes;
        }
        fclose(check);
    }
    br_sha256_out(&context,second);
    hash_hex(record->digest,wanted);hash_hex(actual,got);hash_hex(resident,memory);hash_hex(second,again);
    snprintf(integrity_details,sizeof(integrity_details),
        "codec_integrity codec=%lu source=%s offset=%lu bytes=%lu verified=0 expected=%s actual=%s resident_software=%s second_read_software=%s second_read_ok=%u first_difference=%ld loaded_byte=%02x second_byte=%02x",
        (unsigned long)record->id,hardware?"hardware":"software",(unsigned long)record->offset,(unsigned long)record->bytes,
        wanted,got,memory,again,read_ok,difference==UINT32_MAX?-1L:(long)difference,loaded_byte,read_byte);
}

static bool read_directory(FILE *file, ModuleRecord records[CODEC_MODULE_COUNT], unsigned *count)
{
    uint8_t footer[CODEC_BUNDLE_FOOTER_BYTES], index[CODEC_MODULE_COUNT * CODEC_BUNDLE_ENTRY_BYTES],
        hash[32];
    if (fseek(file, 0, SEEK_END))
        return fail("Cannot seek codec bundle");
    long length = ftell(file);
    if (length < (long)sizeof(footer) || (uint64_t)length > UINT32_MAX)
        return fail("Invalid codec bundle size");
    uint32_t footer_offset = (uint32_t)length - sizeof(footer);
    if (!read_at(file, footer_offset, footer, sizeof(footer)) ||
        memcmp(footer, CODEC_BUNDLE_MAGIC, 8) || read32(footer + 8) != CODEC_BUNDLE_VERSION ||
        read32(footer + 24) != (uint32_t)length || read32(footer + 28))
        return fail("Missing or unsupported codec bundle footer");
    unsigned n = read32(footer + 12);
    uint32_t index_offset = read32(footer + 16), index_bytes = read32(footer + 20);
    if (!n || n > CODEC_MODULE_COUNT || index_bytes != n * CODEC_BUNDLE_ENTRY_BYTES ||
        index_offset > footer_offset || index_bytes != footer_offset - index_offset ||
        (index_offset & 3U))
        return fail("Invalid codec directory bounds");
    if (!read_at(file, index_offset, index, index_bytes))
        return fail("Cannot read codec directory");
    const NveHashPart directory={index,index_bytes};
    int hash_source=hash_parts(&directory,1,hash);
    if(hash_source<0)return false;
    if (memcmp(hash, footer + 32, 32))
        return fail("Codec directory hash mismatch");
    unsigned seen = 0;
    for (unsigned i = 0; i < n; i++) {
        const uint8_t *p = index + i * CODEC_BUNDLE_ENTRY_BYTES;
        ModuleRecord *r = records + i;
        r->id = read32(p);
        r->offset = read32(p + 8);
        r->bytes = read32(p + 12);
        r->image_bytes = read32(p + 16);
        memcpy(r->digest, p + 32, 32);
        if (r->id >= CODEC_MODULE_COUNT || read32(p + 4) != CODEC_MODULE_ABI_VERSION ||
            read32(p + 24) || read32(p + 28) || (r->offset & 3U) || r->bytes < 32 ||
            r->bytes > CODEC_MODULE_MAX_BYTES || !r->image_bytes ||
            r->image_bytes > CODEC_MODULE_MAX_BYTES || read32(p + 20) > CODEC_MODULE_MAX_BYTES ||
            r->offset > index_offset || r->bytes > index_offset - r->offset)
            return fail("Invalid codec module bounds or ABI");
        for (unsigned j = 0; j < i; j++)
            if (records[j].id == r->id || (r->offset < records[j].offset + records[j].bytes &&
                                           records[j].offset < r->offset + r->bytes))
                return fail("Overlapping or duplicate codec modules");
        seen |= 1U << r->id;
        if (expected_mask &&
            (!(expected_mask & (1U << r->id)) || memcmp(expected_hashes[r->id], r->digest, 32)))
            return fail("Player codec version mismatch; restart the player");
    }
    if (expected_mask && seen != expected_mask)
        return fail("Player codec set mismatch; restart the player");
    *count = n;
    return true;
}

static bool image_contains(const LoadedModule *m, const void *pointer, size_t bytes)
{
    uintptr_t value = (uintptr_t)pointer, base = (uintptr_t)m->image;
    return value >= base && value - base <= m->image_bytes &&
           bytes <= m->image_bytes - (value - base);
}

static bool relocate(uint8_t *image, size_t bytes, const uint8_t *metadata, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (!(i & 255U) && !progress(i, count))
            return false;
        uint32_t record = read32(metadata + 4U * i), type = record & 255U, offset = record >> 8;
        if (type == 4U) {
            if (offset)
                return fail("Invalid codec relocation marker");
            continue;
        }
        if (type > 2U || offset > bytes || bytes - offset < 4U)
            return fail("Unsupported or invalid codec relocation");
        if (type == 1U) {
            bool terminated = false;
            while (offset <= bytes - 4U) {
                uint32_t value = read32(image + offset);
                if (value == UINT32_MAX) {
                    terminated = true;
                    break;
                }
                write32(image + offset, value + (uint32_t)(uintptr_t)image);
                offset += 4U;
                if (!(offset & 2047U) && !progress(i, count))
                    return false;
            }
            if (!terminated)
                return fail("Unterminated codec GOT relocation");
        } else
            write32(image + offset,
                    type == 2U ? 0 : read32(image + offset) + (uint32_t)(uintptr_t)image);
    }
    return true;
}

static bool load_image(uint32_t id, LoadedModule *result)
{
    integrity_details[0]=0;
    FILE *file = fopen(bundle_path, "rb");
    if (!file)
        return fail("Cannot open codec bundle");
    ModuleRecord records[CODEC_MODULE_COUNT], *record = NULL;
    unsigned count = 0;
    uint8_t header[32], hash[32];
    uint8_t *metadata = NULL;
    void *allocation = NULL;
    bool okay = false;
    if (!read_directory(file, records, &count))
        goto done;
    for (unsigned i = 0; i < count; i++)
        if (records[i].id == id)
            record = records + i;
    if (!record) {
        fail("Codec module is not in this player");
        goto done;
    }
    if (!read_at(file, record->offset, header, sizeof(header)) || read32(header) != 0x6e68655aU ||
        read32(header + 4) != 1U) {
        fail("Unsupported codec executable");
        goto done;
    }
    uint32_t file_bytes = read32(header + 8), relocs = read32(header + 12),
             flags = read32(header + 16), extra = read32(header + 20);
    uint32_t alloc_bytes = read32(header + 24), entry = read32(header + 28);
    uint64_t prefix64 = 32U + (uint64_t)relocs * 4U + (uint64_t)flags * 4U + extra;
    if (file_bytes != record->bytes || prefix64 > file_bytes || alloc_bytes < file_bytes ||
        alloc_bytes - prefix64 != record->image_bytes || file_bytes - prefix64 < 4U ||
        entry > file_bytes - prefix64 - 4U || (entry & 3U) || prefix64 > 1024U * 1024U) {
        fail("Invalid codec executable sizes");
        goto done;
    }
    uint32_t prefix = (uint32_t)prefix64, initialized = file_bytes - prefix;
    metadata = (uint8_t *)malloc(prefix - 32U ? prefix - 32U : 1U);
    result->allocation_bytes = (size_t)record->image_bytes + 63U;
    allocation = malloc(result->allocation_bytes);
    if (!metadata || !allocation) {
        fail("Not enough memory for codec module");
        goto done;
    }
    result->image = (uint8_t *)(((uintptr_t)allocation + 63U) & ~(uintptr_t)63U);
    result->image_bytes = record->image_bytes;
    bool rolling_software=!hardware_acceleration_enabled(HW_ACCEL_SHA);
    br_sha256_context context;
    if(rolling_software){br_sha256_init(&context);br_sha256_update(&context,header,sizeof(header));}
    for (uint32_t offset = 0; offset < prefix - 32U;) {
        uint32_t bytes = prefix - 32U - offset;
        if (bytes > 65536U)
            bytes = 65536U;
        if (!progress(offset, file_bytes))
            goto done;
        if (!read_at(file, record->offset + 32U + offset, metadata + offset, bytes)) {
            fail("Cannot read codec metadata");
            goto done;
        }
        if(rolling_software)br_sha256_update(&context,metadata+offset,bytes);
        offset += bytes;
    }
    for (uint32_t offset = 0; offset < initialized;) {
        uint32_t bytes = initialized - offset;
        if (bytes > 65536U)
            bytes = 65536U;
        if (!progress(offset, initialized)) {
            goto done;
        }
        if (!read_at(file, record->offset + prefix + offset, result->image + offset, bytes)) {
            fail("Cannot read codec image");
            goto done;
        }
        if(rolling_software)br_sha256_update(&context,result->image+offset,bytes);
        offset += bytes;
    }
    /* Hash the final immutable RAM image in one exclusive hardware lease.
     * File I/O and UI callbacks occur outside the lease; the engine has no
     * portable saved-state import to resume across those native calls. */
    const NveHashPart parts[]={{header,sizeof(header)},{metadata,prefix-32U},{result->image,initialized}};
    int hash_source;
    if(rolling_software){br_sha256_out(&context,hash);hash_source=0;}
    else hash_source=hash_parts(parts,3,hash);
    if(hash_source<0)goto done;
    if (memcmp(hash, record->digest, 32)) {
        diagnose_payload(record,parts,hash,hash_source);
        fail("Codec module hash mismatch");
        goto done;
    }
    snprintf(integrity_details,sizeof(integrity_details),"codec_integrity codec=%lu source=%s bytes=%lu verified=1",
        (unsigned long)id,hash_source?"hardware":"software",(unsigned long)record->bytes);
    for (uint32_t offset = initialized; offset < record->image_bytes;) {
        uint32_t bytes = record->image_bytes - offset;
        if (bytes > 65536U)
            bytes = 65536U;
        if (!progress(offset, record->image_bytes))
            goto done;
        memset(result->image + offset, 0, bytes);
        offset += bytes;
    }
    if (!relocate(result->image, result->image_bytes, metadata, relocs) ||
        !progress(initialized, initialized))
        goto done;
    clear_cache();
    memset(&result->api, 0, sizeof(result->api));
    CodecModuleEntry start = (CodecModuleEntry)(void *)(result->image + entry);
    int status = start(host_api, &result->api);
    if (status || result->api.abi_version != CODEC_MODULE_ABI_VERSION ||
        result->api.struct_size < sizeof(result->api) || result->api.codec_id != id ||
        !result->api.codec_api_size || ((uintptr_t)result->api.codec_api & 3U) ||
        !image_contains(result, result->api.codec_api, result->api.codec_api_size) ||
        !result->api.shutdown || ((uintptr_t)result->api.shutdown & 3U) ||
        !image_contains(result, (void *)(uintptr_t)result->api.shutdown, 4)) {
        if (!status && result->api.shutdown && !((uintptr_t)result->api.shutdown & 3U) &&
            image_contains(result, (void *)(uintptr_t)result->api.shutdown, 4))
            result->api.shutdown();
        fail("Codec module initialization or export ABI failed");
        goto done;
    }
    result->allocation = allocation;
    allocation = NULL;
    okay = true;
done:
    free(metadata);
    free(allocation);
    fclose(file);
    if (!okay)
        memset(result, 0, sizeof(*result));
    return okay;
}

static bool unload(unsigned id)
{
    LoadedModule *m = loaded + id;
    if (!m->allocation)
        return true;
    if (m->references)
        return fail("Codec module still has live decoders");
    if (!m->api.shutdown())
        return fail("Codec module refused to unload");
    void *allocation = m->allocation;
    memset(m, 0, sizeof(*m));
    free(allocation);
    return true;
}

bool codec_modules_configure(const char *path, const CodecHostApi *host)
{
    if (loader_busy || !path || !*path || strlen(path) >= sizeof(bundle_path) || !host ||
        host->abi_version != CODEC_MODULE_ABI_VERSION || host->struct_size < sizeof(*host) ||
        !host->resolve)
        return fail("Invalid codec module configuration");
    if (!codec_modules_shutdown())
        return false;
    strcpy(bundle_path, path);
    host_api = host;
    last_error[0] = 0;
    return true;
}

void codec_modules_set_progress(CodecModuleProgress callback, void *userdata)
{
    if (!loader_busy) {
        progress_callback = callback;
        progress_userdata = userdata;
    }
}

bool codec_modules_set_expected_hashes(const uint8_t hashes[CODEC_MODULE_COUNT][32],
                                       uint32_t present_mask)
{
    if (loader_busy || (present_mask & ~((1U << CODEC_MODULE_COUNT) - 1U)) ||
        (present_mask && !hashes))
        return fail("Invalid expected codec hashes");
    for (unsigned i = 0; i < CODEC_MODULE_COUNT; i++)
        if (loaded[i].allocation)
            return fail("Codec hashes cannot change while modules are loaded");
    if (present_mask)
        memcpy(expected_hashes, hashes, sizeof(expected_hashes));
    expected_mask = present_mask;
    return true;
}

const CodecModuleApi *codec_module_acquire(uint32_t id)
{
    if (id >= CODEC_MODULE_COUNT || loader_busy || !host_api || !bundle_path[0]) {
        fail("Codec module loader is unavailable");
        return NULL;
    }
    loader_busy = true;
    last_error[0] = 0;
    for (unsigned i = 0; i < CODEC_MODULE_COUNT; i++)
        if (i != id && loaded[i].allocation && !loaded[i].references)
            if (!unload(i)) {
                loader_busy = false;
                return NULL;
            }
    LoadedModule *m = loaded + id;
    if (!m->allocation && !load_image(id, m)) {
        loader_busy = false;
        return NULL;
    }
    if (m->references == UINT_MAX) {
        fail("Too many codec module references");
        loader_busy = false;
        return NULL;
    }
    m->references++;
    active_codec = id;
    loader_busy = false;
    return &m->api;
}

void codec_module_release(uint32_t id)
{
    if (!loader_busy && id < CODEC_MODULE_COUNT && loaded[id].references) {
        loaded[id].references--;
        if (!loaded[id].references && id != active_codec) {
            loader_busy = true;
            unload(id);
            loader_busy = false;
        }
    }
}

bool codec_modules_trim_idle(void)
{
    if (loader_busy)
        return fail("Codec loader is busy");
    loader_busy = true;
    bool okay = true;
    for (unsigned i = 0; i < CODEC_MODULE_COUNT; i++)
        if (!loaded[i].references && !unload(i))
            okay = false;
    loader_busy = false;
    return okay;
}

bool codec_modules_shutdown(void)
{
    for (unsigned i = 0; i < CODEC_MODULE_COUNT; i++)
        if (loaded[i].references)
            return fail("Codec module still has live decoders");
    if (!codec_modules_trim_idle())
        return false;
    bundle_path[0] = 0;
    host_api = NULL;
    active_codec = CODEC_MODULE_COUNT;
    expected_mask = 0;
    progress_callback = NULL;
    progress_userdata = NULL;
    last_error[0] = 0;
    return true;
}

const char *codec_module_error(void)
{
    return last_error;
}
const char *codec_module_integrity_details(void){return integrity_details;}

size_t codec_modules_loaded_bytes(void)
{
    size_t total = 0;
    for (unsigned i = 0; i < CODEC_MODULE_COUNT; i++)
        total += loaded[i].allocation_bytes;
    return total;
}

unsigned codec_module_references(uint32_t id)
{
    return id < CODEC_MODULE_COUNT ? loaded[id].references : 0;
}
#endif
