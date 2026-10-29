#ifndef FENESTRA_ACCOUNTING_SDK_H
#define FENESTRA_ACCOUNTING_SDK_H
#include "fenestra_sdk.h"
#include "fenestra_accounting.h"
static inline FxResult fx_sdk_account(FxSdkClient *c,const FxServiceModule *api,uint64_t binding,uint32_t operation,uint64_t after) {
    return fx_sdk_platform(c,api,FX_BIND_CALL,binding,operation,after,0,0u);
}
#endif
