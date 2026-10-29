#ifndef FENESTRA_LZSS_H
#define FENESTRA_LZSS_H
#include <stdint.h>
/* Literal/reference groups: eight low-first flag bits, 1=literal; a little
 * endian reference stores (distance-1):12, (length-3):4. Exact output size is
 * external. No unchecked input/output access, imports, or heap allocations. */
uint32_t fx_lzss_unpack(uint8_t *out,uint32_t bytes,const uint8_t *in,uint32_t size);
/* Optional owned AArch64 payload preprocessing. LZSS decoding alone does
 * not restore a filtered payload; reverse B/BL PC normalization before use. */
void fx_arm64_branch_restore(uint8_t *data,uint32_t bytes);
#endif
