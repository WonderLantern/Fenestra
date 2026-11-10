#include <assert.h>
#include <string.h>
#include "owner_storage.h"
int main(void) {
    FxNativeDescriptor stock={0};
    stock.flags=0x80001002u;stock.header=0x43b38c80u;
    stock.rows=0x43b38d90u;stock.events=0x43b395a8u;stock.count=47u;
    for(unsigned i=0;i<15;i++)stock.geometry[i]=(uint16_t)(i*17u);
    FxNativePage page={0};page.identity=0x02a00002u;page.descriptor=(uintptr_t)&stock;
    FxNativePage before=page;FxNativeDescriptor original=stock;
    FxZfOwnerStorage owner={0};
    assert(fx_zf_owner_storage_prepare(&owner,&page,&stock)==FX_OK);
    assert(!memcmp(&owner.stock,&stock,sizeof(stock)));
    assert(!memcmp(&page,&before,sizeof(page)) && !memcmp(&stock,&original,sizeof(stock)));
    assert(owner.page==(uintptr_t)&page && owner.original_descriptor==(uintptr_t)&stock);
    assert(!atomic_load(&owner.lifecycle.word));
    assert(fx_zf_owner_storage_prepare(&owner,&page,&stock)==FX_BUSY);
    FxZfOwnerStorage empty={0};page.identity^=1;
    assert(fx_zf_owner_storage_prepare(&empty,&page,&stock)==FX_INTEGRITY);
    assert(!memcmp(&empty,&(FxZfOwnerStorage){0},sizeof(empty)));
    page=before;stock.count=48;
    assert(fx_zf_owner_storage_prepare(&empty,&page,&stock)==FX_INTEGRITY);
    stock=original;page.descriptor++;
    assert(fx_zf_owner_storage_prepare(&empty,&page,&stock)==FX_INTEGRITY);
    page=before;
    assert(fx_zf_owner_storage_prepare(&empty,&page,&empty.stock)==FX_ARGUMENT);
    assert(fx_zf_owner_storage_prepare(0,&page,&stock)==FX_ARGUMENT);
    return 0;
}
