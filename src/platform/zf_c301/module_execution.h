#ifndef FENESTRA_ZF_C301_MODULE_EXECUTION_H
#define FENESTRA_ZF_C301_MODULE_EXECUTION_H

#include "owner_storage.h"

FxResult fx_zf_owner_module_prepare(FxZfOwnerStorage *);
FxResult fx_zf_owner_module_reserve_image(FxZfOwnerStorage *,uint32_t bytes);
FxResult fx_zf_owner_module_publish(FxZfOwnerStorage *);

#endif
