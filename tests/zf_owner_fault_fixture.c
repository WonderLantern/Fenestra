#include <assert.h>
#include "owner_storage.h"
FxZfLifecycle *fx_zf_runtime_lifecycle(void){return 0;}
int main(void) {
    fx_zf_owner_fault(0,0,FX_IO);
    FxZfOwnerStorage owner={0};FxPlatformOwner module={0};module.owner=42u;
    fx_zf_lifecycle_record(&owner.lifecycle,20u);fx_zf_lifecycle_record(&owner.lifecycle,25u);
    assert(fx_zf_lifecycle_accepts_work(&owner.lifecycle));
    fx_zf_owner_fault(&owner,&module,FX_IO);
    assert(atomic_load(&owner.first_fault)==((UINT64_C(42)<<32)|FX_IO));
    assert(!fx_zf_lifecycle_accepts_work(&owner.lifecycle));
    module.owner=99u;fx_zf_owner_fault(&owner,&module,FX_BOUNDS);
    assert(atomic_load(&owner.first_fault)==((UINT64_C(42)<<32)|FX_IO));
    fx_zf_lifecycle_record(&owner.lifecycle,20u);fx_zf_lifecycle_record(&owner.lifecycle,25u);
    assert(atomic_load(&owner.lifecycle.word)&FX_ZF_LIFE_STOP);
    FxZfOwnerStorage other={0};fx_zf_owner_fault(&other,0,FX_OK);
    assert(atomic_load(&other.first_fault)==FX_INTEGRITY);
    return 0;
}
