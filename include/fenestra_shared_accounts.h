#ifndef FENESTRA_SHARED_ACCOUNTS_H
#define FENESTRA_SHARED_ACCOUNTS_H
#include "fenestra_accounting.h"
/* Embedded in the retained controller, initialized only during activation. */
typedef struct {FxAccountState state;FxAccountRow controller,bus,pages,domain,ui,text,loader,indexes,inspect;_Atomic uint32_t ready;} FxSharedAccounts;
FxResult fx_shared_accounts_init(FxSharedAccounts *,FxIndexMemory,uint32_t controller_bytes,uint32_t task_domain_bytes);
/* Target controller accessor. An unactivated controller has no live ledger. */
FxAccountState *fx_native_shared_accounts(void);
FxRetainedAccount fx_shared_ui_account(FxSharedAccounts *);
FxRetainedAccount fx_shared_text_account(FxSharedAccounts *);
FxRetainedAccount fx_shared_loader_account(FxSharedAccounts *);
static inline FxAccountRow *fx_shared_account_row(FxAccountState *state,const char *name) {
    FxAccountRow *row=0;if(state)(void)fx_account_get(state,name,FX_ACCOUNT_MANAGED|FX_ACCOUNT_SHARED,0u,&row);return row;
}
#endif
