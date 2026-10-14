#ifndef NDVIDEO_NVE_ACCEL_H
#define NDVIDEO_NVE_ACCEL_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct { const void *data; size_t bytes; } NveHashPart;
#if defined(_TINSPIRE)
/* 1: exclusive hardware lease, 0: model without this engine, -1: failure.
 * A lease never survives a cooperative yield or native/OS call. */
int nve_accel_begin(void);
int nve_accel_hmac(const uint8_t key[64], const NveHashPart *, unsigned, uint8_t out[32]);
int nve_accel_hash(const NveHashPart *, unsigned, uint8_t out[32]);
bool nve_accel_end(void);
#else
static inline int nve_accel_begin(void) { return 0; }
static inline int nve_accel_hmac(const uint8_t *k,const NveHashPart *p,unsigned n,uint8_t *o)
{ (void)k;(void)p;(void)n;(void)o;return 0; }
static inline int nve_accel_hash(const NveHashPart *p,unsigned n,uint8_t *o)
{(void)p;(void)n;(void)o;return 0;}
static inline bool nve_accel_end(void) { return true; }
#endif
#endif
