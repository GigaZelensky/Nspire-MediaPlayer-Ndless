#ifndef NDVIDEO_NVE_CRYPTO_H
#define NDVIDEO_NVE_CRYPTO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "crypto/bearssl/bearssl_block.h"
#include "crypto/bearssl/bearssl_hmac.h"

/* NVE1 wraps a complete NVP stream. All integers are little-endian.
 * Header: magic[4], version:u32, record_bytes:u32, iterations:u32,
 * plaintext_bytes:u64, duration_ms:u32, frame_count:u32, salt[16], nonce[12],
 * fps_num:u32, fps_den:u32, codec_hint:u8, reserved[27], header_tag[32].
 * Version 1 has a zero hint; version 2 uses 1=H.264, 2=MPEG-4, 3=HEVC, 4=AV1.
 * The hint is public and authenticated with the rest of the header; reserved bytes are zero.
 * Records: up to 16384 ciphertext bytes followed by a 32-byte HMAC.
 * Versions 1/2 use nonce[12] || BE32(plaintext_offset / 16) and eight-byte
 * record metadata (LE32 record index, LE32 length), limited to 64 GiB of CTR
 * plaintext. Version 3 uses nonce[8] || BE64(plaintext_offset / 16), with the
 * remaining four stored nonce bytes zero, and LE64 index + LE32 length.
 * Plain/physical sizes and runtime record positions are 64-bit throughout.
 * PBKDF2-HMAC-SHA256 derives a 32-byte master; domain-separated HMACs
 * derive independent encryption and authentication keys. Tags bind the
 * authenticated header, record number, length and ciphertext. No plaintext
 * is released until its complete record is authenticated. */
#define NVE_HEADER_BYTES 128U
#define NVE_HEADER_AUTH_BYTES 96U
#define NVE_RECORD_BYTES 16384U
#define NVE_TAG_BYTES 32U
#define NVE_KDF_ROUNDS 600000U
#define NVE_PASSWORD_MAX 128U

typedef struct NveHeader {
    uint8_t bytes[NVE_HEADER_BYTES];
    uint64_t plain_bytes, physical_bytes;
    uint32_t duration_ms, frame_count, fps_num, fps_den;
    uint8_t codec_hint;
} NveHeader;
typedef struct {
    NveHeader header;
    br_aes_ct_ctr_keys aes;
    br_hmac_key_context mac;
    uint8_t mac_key[64];
    bool valid;
} NveKeys;
typedef struct {
    NveHeader header;
    br_hmac_key_context password;
    uint8_t password_key[64];
    uint8_t u[32], master[32];
    uint32_t rounds;
    bool active, failed;
} NveKdf;

void nve_wipe(void *data, size_t bytes);
bool nve_header_parse(NveHeader *, const void *bytes, uint64_t physical_bytes);
bool nve_kdf_begin(NveKdf *, const NveHeader *, const void *password, size_t bytes);
bool nve_kdf_step(NveKdf *, unsigned rounds);
bool nve_kdf_finish(NveKdf *, NveKeys *);

typedef enum { NVE_READ_IDLE, NVE_READ_FETCH, NVE_READ_VERIFY, NVE_READ_DECRYPT,
               NVE_READ_COPY, NVE_READ_DONE, NVE_READ_ERROR } NveReadPhase;
typedef enum { NVE_ERROR_NONE, NVE_ERROR_KEYS, NVE_ERROR_IO,
               NVE_ERROR_AUTH, NVE_ERROR_STATE, NVE_ERROR_ACCELERATOR } NveReadError;
typedef struct {
    const NveKeys *keys;
    uint8_t block[NVE_RECORD_BYTES + NVE_TAG_BYTES];
    br_hmac_context mac;
    uint8_t *destination;
    uint64_t offset, unit, cached_unit;
    uint32_t bytes, copied, unit_bytes, progress;
    NveReadPhase phase;
    NveReadError error;
    NveReadPhase error_phase;
    uint8_t error_cipher_hash[32], error_tag[32];
} NveReader;
void nve_reader_init(NveReader *, const NveKeys *);
bool nve_reader_begin(NveReader *, uint64_t offset, void *destination, uint32_t bytes);
uint64_t nve_reader_physical_offset(const NveReader *);
void nve_reader_supplied(NveReader *, bool success);
void nve_reader_step(NveReader *);
/* Cancel publication and discard any partly read/decrypted record. */
void nve_reader_cancel(NveReader *);
#endif
