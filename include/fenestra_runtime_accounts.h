#ifndef FENESTRA_RUNTIME_ACCOUNTS_H
#define FENESTRA_RUNTIME_ACCOUNTS_H
#include "fenestra_accounting.h"
/* No allocation to initialize: the enclosing native worker/service owns
 * these records. Source scratch is separately owned and charged once. */
typedef struct {FxAccountState state;FxAccountRow allocation,source;} FxRuntimeAccounts;
static inline FxResult fx_runtime_accounts_init(FxRuntimeAccounts *r,uint32_t owner,uint32_t generation,FxIndexMemory memory) {
    if(!r)return FX_ARGUMENT;
    FxResult result=fx_account_init(&r->state,owner,generation,memory);if(result!=FX_OK)return result;
    result=fx_account_register_static(&r->state,&r->allocation,"runtime.allocation",FX_ACCOUNT_MANAGED,0u);
    if(result==FX_OK)result=fx_account_register_static(&r->state,&r->source,"module.load-source",FX_ACCOUNT_MANAGED,0u);
    /* Cleared only after all core allocation providers have been integrated. */
    atomic_fetch_or(&r->state.flags,FX_ACCOUNT_PARTIAL);return result;
}
#endif
