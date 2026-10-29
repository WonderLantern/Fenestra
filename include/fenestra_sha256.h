#ifndef FENESTRA_SHA256_H
#define FENESTRA_SHA256_H
#include "fenestra.h"
/* Incremental data-only hashing for bounded staging of large resource files. */
typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    uint32_t used;
    uint8_t tail[64];
} FxSha256;
void fx_sha256_init(FxSha256 *);
FxResult fx_sha256_update(FxSha256 *,const void *,size_t);
void fx_sha256_final(const FxSha256 *,uint8_t [32]);
#endif
