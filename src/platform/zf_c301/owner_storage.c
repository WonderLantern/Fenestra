#if !defined(FX_ZF_EXPERIMENTAL_OWNER_STORAGE) || FX_ZF_EXPERIMENTAL_OWNER_STORAGE != 1
#error "Zf owner storage is experimental and must not publish native pointers"
#endif
#include "owner_storage.h"
#include "fenestra_memory.h"

_Static_assert(offsetof(FxZfOwnerStorage,stock)==0 && sizeof(FxNativeDescriptor)==64,
               "Zf retained owner must preserve the original descriptor prefix");
static int overlaps(uintptr_t a,size_t an,uintptr_t b,size_t bn) {
    return a>UINTPTR_MAX-an || b>UINTPTR_MAX-bn || (a<b+bn && b<a+an);
}
FxResult fx_zf_owner_storage_prepare(FxZfOwnerStorage *out,const FxNativePage *page,
                                    const FxNativeDescriptor *stock) {
    if(!out || !page || !stock || (uintptr_t)out%_Alignof(FxZfOwnerStorage) ||
       (uintptr_t)page%_Alignof(FxNativePage) || (uintptr_t)stock%_Alignof(FxNativeDescriptor))return FX_ARGUMENT;
    if(overlaps((uintptr_t)out,sizeof(*out),(uintptr_t)page,sizeof(*page)) ||
       overlaps((uintptr_t)out,sizeof(*out),(uintptr_t)stock,sizeof(*stock)))return FX_ARGUMENT;
    const uint8_t *bytes=(const void *)out;
    for(size_t i=0;i<sizeof(*out);i++)if(bytes[i])return FX_BUSY;
    if(page->identity!=UINT32_C(0x02a00002) ||
       atomic_load_explicit((const _Atomic uintptr_t *)&page->descriptor,memory_order_acquire)!=(uintptr_t)stock ||
       stock->flags!=UINT32_C(0x80001002) || stock->reserved || stock->count!=47u ||
       stock->header!=UINT64_C(0x43b38c80) || stock->rows!=UINT64_C(0x43b38d90) ||
       stock->events!=UINT64_C(0x43b395a8))return FX_INTEGRITY;
    /* Preserve every native field, including geometry and reserved tail. No
     * application rows/text or global roots are changed by preparation. */
    memcpy(&out->stock,stock,sizeof(*stock));
    out->page=(uintptr_t)page;
    out->original_descriptor=(uintptr_t)stock;
    out->version=12u;
    out->magic=UINT32_C(0x46585a46);
    return FX_OK;
}
