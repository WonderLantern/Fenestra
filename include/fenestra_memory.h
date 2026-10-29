#ifndef FENESTRA_MEMORY_H
#define FENESTRA_MEMORY_H
#include <stddef.h>
void *fx_memset(void *, int value, size_t bytes);
void *fx_memcpy(void *, const void *, size_t bytes);
void *fx_memmove(void *, const void *, size_t bytes);
int fx_memcmp(const void *, const void *, size_t bytes);
/* Ordinary owned RAM only: word accesses are allowed. These are deliberately
 * separate from byte helpers used by arbitrary-address/debug/device paths.
 * Copy has memcpy (non-overlap) semantics; equality still checks ALL bytes. */
void *fx_ram_copy(void *,const void *,size_t bytes);
void fx_ram_zero(void *,size_t bytes);
int fx_ram_equal(const void *,const void *,size_t bytes);
#define memset fx_memset
#define memcpy fx_memcpy
#define memmove fx_memmove
#define memcmp fx_memcmp
#endif
