#include <stddef.h>
#include <stdint.h>
void *fx_memset(void *destination, int value, size_t bytes) {
    uint8_t *p = destination;
    while (bytes != 0u) { *p++ = (uint8_t)value; --bytes; }
    return destination;
}
void *fx_memcpy(void *destination, const void *source, size_t bytes) {
    uint8_t *p = destination;
    const uint8_t *q = source;
    while (bytes != 0u) { *p++ = *q++; --bytes; }
    return destination;
}
void *fx_memmove(void *destination,const void *source,size_t bytes) {
    uint8_t *p=destination;const uint8_t *q=source;
    if((uintptr_t)p>(uintptr_t)q && (uintptr_t)p-(uintptr_t)q<bytes) {
        while(bytes){--bytes;p[bytes]=q[bytes];}
    } else {while(bytes){*p++=*q++;--bytes;}}
    return destination;
}
int fx_memcmp(const void *left, const void *right, size_t bytes) {
    const uint8_t *p = left, *q = right;
    while (bytes != 0u) {
        if (*p != *q) return (int)*p - (int)*q;
        ++p; ++q; --bytes;
    }
    return 0;
}
/* The supported GNU/Clang native toolchains provide a byte-alias-safe word
 * type. Never cast an unaligned pointer, overread a tail, or access beyond the
 * supplied object. Other compilers keep the byte implementation. */
#if defined(__GNUC__) || defined(__clang__)
typedef uint64_t FxRamWord __attribute__((__may_alias__));
#define FX_RAM_WORDS 1
#endif
void *fx_ram_copy(void *destination,const void *source,size_t bytes) {
    uint8_t *p=destination;const uint8_t *q=source;
    if(p==q)return destination;
#ifdef FX_RAM_WORDS
    if((((uintptr_t)p|(uintptr_t)q)&7u)==0u) {
        FxRamWord *d=(FxRamWord *)p;const FxRamWord *s=(const FxRamWord *)q;
        while(bytes>=32u){d[0]=s[0];d[1]=s[1];d[2]=s[2];d[3]=s[3];d+=4;s+=4;bytes-=32u;}
        while(bytes>=8u){*d++=*s++;bytes-=8u;}
        p=(uint8_t *)d;q=(const uint8_t *)s;
    }
#endif
    while(bytes){*p++=*q++;--bytes;}return destination;
}
void fx_ram_zero(void *destination,size_t bytes) {
    uint8_t *p=destination;
#ifdef FX_RAM_WORDS
    while(bytes && ((uintptr_t)p&7u)){*p++=0u;--bytes;}
    if(!bytes)return;
    FxRamWord *d=(FxRamWord *)p;
    while(bytes>=32u){d[0]=0u;d[1]=0u;d[2]=0u;d[3]=0u;d+=4;bytes-=32u;}
    while(bytes>=8u){*d++=0u;bytes-=8u;}p=(uint8_t *)d;
#endif
    while(bytes){*p++=0u;--bytes;}
}
int fx_ram_equal(const void *left,const void *right,size_t bytes) {
    const uint8_t *p=left,*q=right;
#ifdef FX_RAM_WORDS
    if((((uintptr_t)p|(uintptr_t)q)&7u)==0u) {
        const FxRamWord *a=(const FxRamWord *)p,*b=(const FxRamWord *)q;
        while(bytes>=8u){if(*a++!=*b++)return 0;bytes-=8u;}
        p=(const uint8_t *)a;q=(const uint8_t *)b;
    }
#endif
    while(bytes){if(*p++!=*q++)return 0;--bytes;}return 1;
}
#ifdef FX_FREESTANDING
/* Compilers may emit ABI memory calls for struct assignments even with
 * -ffreestanding. These symbols resolve to our implementation, not libc. */
void *memcpy(void *destination, const void *source, size_t bytes) {
    return fx_memcpy(destination, source, bytes);
}
void *memset(void *destination, int value, size_t bytes) {
    return fx_memset(destination, value, bytes);
}
void *memmove(void *destination,const void *source,size_t bytes) {
    return fx_memmove(destination,source,bytes);
}
int memcmp(const void *left, const void *right, size_t bytes) {
    return fx_memcmp(left, right, bytes);
}
#endif
