#ifndef FENESTRA_ZF_C301_CARD_H
#define FENESTRA_ZF_C301_CARD_H
#include "fenestra_file_card.h"
typedef struct {
    int32_t (*open)(const char *,uint32_t,uint32_t);
    uint32_t (*read)(int32_t,void *,uint32_t,uint32_t *);
    uint32_t (*close)(int32_t);
} FxZfCardNative;
/* A single owner/worker owns each scope. Never expose native fd to modules. */
typedef struct {
    uint32_t magic,uncertain,native_error;
    int32_t fd;
    const FxZfCardNative *native;
} FxZfCardScope;
FxResult fx_zf_card_files(FxTextFiles *,FxZfCardScope *,const FxZfCardNative *);
FxResult fx_zf_card_native_files(FxTextFiles *,FxZfCardScope *);
const FxZfCardNative *fx_zf_card_native_ops(void);
#endif
